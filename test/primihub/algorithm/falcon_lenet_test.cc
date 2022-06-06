// Copyright [2021] <primihub.com>

#include "gtest/gtest.h"

#include "src/primihub/algorithm/Falcon_Lenet.h"
#include "src/primihub/service/dataset/localkv/storage_default.h"

using namespace primihub;

static void RunFalconlenet(std::string node_id, rpc::Task &task,
                        std::shared_ptr<DatasetService> data_service) {
  PartyConfig config(node_id, task);
  LogisticRegressionExecutor exec(config, data_service);
  EXPECT_EQ(exec.loadParams(task), 0);
  EXPECT_EQ(exec.initPartyComm(), 0);
  EXPECT_EQ(exec.loadDataset(), 0);
  EXPECT_EQ(exec.execute(), 0);
  EXPECT_EQ(exec.saveModel(), 0);
  exec.finishPartyComm();
}

TEST(falcon, falcon_lenet_test) {
  rpc::Node node_1;
  node_1.set_node_id("node_1");
  node_1.set_ip("127.0.0.1");

  rpc::VirtualMachine *vm = node_1.add_vm();
  vm->set_party_id(0);

  rpc::EndPoint *next = vm->mutable_next();
  rpc::EndPoint *prev = vm->mutable_prev();
  next->set_ip("127.0.0.1");
  next->set_port(8000);
  prev->set_ip("127.0.0.1");
  prev->set_port(8100);

  rpc::Node node_2;
  node_2.set_node_id("node_2");
  node_2.set_ip("127.0.0.1");

  vm = node_2.add_vm();
  vm->set_party_id(1);

  next = vm->mutable_next();
  prev = vm->mutable_prev();
  next->set_ip("127.0.0.1");
  next->set_port(8200);
  prev->set_ip("127.0.0.1");
  prev->set_port(8000);

  rpc::Node node_3;
  node_3.set_node_id("node_3");
  node_3.set_ip("127.0.0.1");

  vm = node_3.add_vm();
  vm->set_party_id(2);

  next = vm->mutable_next();
  prev = vm->mutable_prev();
  next->set_ip("127.0.0.1");
  next->set_port(8100);
  prev->set_ip("127.0.0.1");
  prev->set_port(8200);

  // Construct task for party 0.
  rpc::Task task1;
  auto node_map = task1.mutable_node_map();
  (*node_map)["node_1"] = node_1;
  (*node_map)["node_2"] = node_2;
  (*node_map)["node_3"] = node_3;
  task1.set_task_id("mpc_lenet");//
  task1.set_job_id("lenet_job");//

/*外部地址写死了，不必加在这里*/
//   rpc::ParamValue pv_train_input;
//   pv_train_input.set_var_type(rpc::VarType::STRING);
//   pv_train_input.set_value_string("/tmp/train_party_0.csv");

//   rpc::ParamValue pv_test_input;
//   pv_test_input.set_var_type(rpc::VarType::STRING);
//   pv_test_input.set_value_string("/tmp/test_party_0.csv");

  rpc::ParamValue pv_batch_size;
  pv_batch_size.set_var_type(rpc::VarType::INT32);
  pv_batch_size.set_value_int32(128);

  rpc::ParamValue pv_num_iter;
  pv_num_iter.set_var_type(rpc::VarType::INT32);
  pv_num_iter.set_value_int32(100);

  auto param_map = task1.mutable_params()->mutable_param_map();
//   (*param_map)["TrainData"] = pv_train_input;
//   (*param_map)["TestData"] = pv_test_input;
  (*param_map)["NumIters"] = pv_num_iter;
  (*param_map)["BatchSize"] = pv_batch_size;

  // Construct task for party 1.
  rpc::Task task2;
  node_map = task2.mutable_node_map();
  (*node_map)["node_1"] = node_1;
  (*node_map)["node_2"] = node_2;
  (*node_map)["node_3"] = node_3;
  task2.set_task_id("mpc_lenet");
  task2.set_job_id("lenet_job");

//   pv_train_input.set_value_string("/tmp/train_party_1.csv");
//   pv_test_input.set_value_string("/tmp/test_party_1.csv");
  param_map = task2.mutable_params()->mutable_param_map();
//   (*param_map)["TrainData"] = pv_train_input;
//   (*param_map)["TestData"] = pv_test_input;
  (*param_map)["NumIters"] = pv_num_iter;
  (*param_map)["BatchSize"] = pv_batch_size;

  // Construct task for party 2.
  rpc::Task task3;
  node_map = task3.mutable_node_map();
  (*node_map)["node_1"] = node_1;
  (*node_map)["node_2"] = node_2;
  (*node_map)["node_3"] = node_3;
  task3.set_task_id("mpc_lenet");
  task3.set_job_id("lenet_job");

//   pv_train_input.set_value_string("/tmp/train_party_2.csv");
//   pv_test_input.set_value_string("/tmp/test_party_2.csv");
  param_map = task3.mutable_params()->mutable_param_map();
//   (*param_map)["TrainData"] = pv_train_input;
//   (*param_map)["TestData"] = pv_test_input;
  (*param_map)["NumIters"] = pv_num_iter;
  (*param_map)["BatchSize"] = pv_batch_size;

  pid_t pid = fork();
  if (pid != 0) {
    // Child process as party 0.
    auto stub = std::make_shared<p2p::NodeStub>();
    stub->start("/ip4/127.0.0.1/tcp/8888");
    std::shared_ptr<DatasetService> service = std::make_shared<DatasetService>(
        stub, std::make_shared<service::StorageBackendDefault>());

    google::InitGoogleLogging("LENET-Party0");
    RunFalconlenet("node_1", task1, service);
    return;
  }

  pid = fork();
  if (pid != 0) {
    // Child process as party 1.
    sleep(1);
    auto stub = std::make_shared<p2p::NodeStub>();
    stub->start("/ip4/127.0.0.1/tcp/8889");
    std::shared_ptr<DatasetService> service = std::make_shared<DatasetService>(
        stub, std::make_shared<service::StorageBackendDefault>());
    
    google::InitGoogleLogging("LENET-party1");
    RunFalconlenet("node_2", task2, service);
    return;
  }

  // Parent process as party 2.
  sleep(3);
  auto stub = std::make_shared<p2p::NodeStub>();
  stub->start("/ip4/127.0.0.1/tcp/8890");
  std::shared_ptr<DatasetService> service = std::make_shared<DatasetService>(
      stub, std::make_shared<service::StorageBackendDefault>());
  
  google::InitGoogleLogging("LENET-party2");
  RunFalconlenet("node_3", task3, service);
  return;
}
