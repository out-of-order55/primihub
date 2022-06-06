/*
 Copyright 2022 Primihub

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      https://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */
#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "absl/strings/str_cat.h"

#include "src/primihub/task/semantic/scheduler/fl_scheduler.h"
#include "src/primihub/task/language/py_parser.h"
#include "src/primihub/protos/common.pb.h"


using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using primihub::rpc::Task;
using primihub::rpc::Params;
using primihub::rpc::ParamValue;
using primihub::rpc::VarType;
using primihub::rpc::TaskType;
using primihub::rpc::VirtualMachine;
using primihub::rpc::EndPoint;
using primihub::rpc::LinkType;

namespace primihub::task {


    void nodeContext2TaskParam(NodeContext node_context, PushTaskRequest* node_task_request) {
        google::protobuf::Map<std::string, ParamValue> *params_map = 
                    node_task_request->mutable_task()->mutable_params()->mutable_param_map();
        // Role
        ParamValue pv_role;
        pv_role.set_var_type(VarType::STRING);
        pv_role.set_value_string(node_context.role);
        (*params_map)["role"] = pv_role;
        // protocol
        ParamValue pv_protocol;
        pv_protocol.set_var_type(VarType::STRING);
        pv_protocol.set_value_string(node_context.protocol);
        (*params_map)["protocol"] = pv_protocol;
        // datasets
        for (size_t i = 0; i < node_context.datasets.size(); i++) {
           node_task_request->mutable_task()->add_input_datasets(node_context.datasets[i]);
        }
        // dumps code
        node_task_request->mutable_task()->set_code(node_context.dumps_func);
    }

    void push_node_py_task(const std::string &node_id,
                          const std::string &role,
                          const std::string &dest_node_address,
                          const PushTaskRequest &nodePushTaskRequest,
                          const PeerContextMap peer_context_map) {
        ClientContext context;
        PushTaskReply pushTaskReply;
        PushTaskRequest _1NodePushTaskRequest;
        _1NodePushTaskRequest.CopyFrom(nodePushTaskRequest);
        NodeContext peer_context = peer_context_map.find(role)->second;
        nodeContext2TaskParam(peer_context, &_1NodePushTaskRequest);

        std::unique_ptr<VMNode::Stub> stub_ = VMNode::NewStub(grpc::CreateChannel(
            dest_node_address, grpc::InsecureChannelCredentials()));
        Status status =
            stub_->SubmitTask(&context, _1NodePushTaskRequest, &pushTaskReply);

        if (status.ok()) {
            LOG(INFO) << "Node push task rpc succeeded.";
        } else {
            LOG(ERROR) << "Node push task rpc failed.";
        }
    }

    /**
     * @brief Dispatch FL task to diffent role. eg: xgboost host & guest.
     * 
     */
    void FLScheduler::dispatch(const PushTaskRequest *pushTaskRequest) {
        
        PushTaskRequest nodePushTaskRequest;
        nodePushTaskRequest.CopyFrom(*pushTaskRequest);
        // Construct node map
        if (pushTaskRequest->task().type() == TaskType::ACTOR_TASK) {
            google::protobuf::Map<std::string, Node> *mutable_node_map =
                nodePushTaskRequest.mutable_task()->mutable_node_map();
            nodePushTaskRequest.mutable_task()->set_type(TaskType::NODE_TASK);
            
            // role: host -> party 0   role: guest -> party 1
            for (size_t i = 0; i < this->peers_with_tag_.size(); i++) {
                Node single_node;

                single_node.CopyFrom(this->peers_with_tag_[i].first);
                std::string node_id = this->peers_with_tag_[i].first.node_id();
                std::string role = this->peers_with_tag_[i].second;
                
                int party_id = 0;
                if (role == "host") {
                   party_id = 0;
                } else if (role == "guest") {
                    party_id = 1;
                }
                (*mutable_node_map)[node_id] = single_node;
                add_vm(&single_node, party_id, 2, &nodePushTaskRequest);

                (*mutable_node_map)[node_id] = single_node;
            }
        }

        // schedule
        std::vector<std::thread> thrds;
       for (size_t i = 0; i < peers_with_tag_.size(); i++) {
            NodeWithRoleTag peer_with_tag = peers_with_tag_[i];
          
            std::string dest_node_address(
                    absl::StrCat(peer_with_tag.first.ip(), ":", peer_with_tag.first.port()));
            LOG(INFO) << "dest_node_address: " << dest_node_address;
            
            thrds.emplace_back(std::thread(push_node_py_task,
                                               peer_with_tag.first.node_id(),    // node_id
                                               peer_with_tag.second,             // role
                                               dest_node_address,               // dest_node_address
                                               std::ref(nodePushTaskRequest), // nodePushTaskRequest
                                               this->peer_context_map_
                                               ));
        }

        for (auto &t : thrds) {
            t.join();
        }
    }


    void FLScheduler::add_vm(Node *node, int i, int role_num, 
                            const PushTaskRequest *pushTaskRequest) {
        VirtualMachine *vm = node->add_vm();
        vm->set_party_id(i);
        EndPoint *ed_next = vm->mutable_next();

        auto next = (i + 1) % role_num;
        
        std::string name_prefix = pushTaskRequest->task().job_id() + "_" +
                                pushTaskRequest->task().task_id() + "_";

        int session_basePort = 12120;
        ed_next->set_ip(peers_with_tag_[next].first.ip());
        ed_next->set_port(std::min(i, next) + session_basePort);
        ed_next->set_name(name_prefix +
                        absl::StrCat(std::min(i, next), std::max(i, next)));
        ed_next->set_link_type(i < next ? LinkType::SERVER : LinkType::CLIENT);
    }



} // namespace primihub::task
