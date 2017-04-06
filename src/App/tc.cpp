//
// Copyright (c) 2016-2017 by Cornell University.  All Rights Reserved.
//
// Permission to use the "TownCrier" software ("TownCrier"), officially docketed at
// the Center for Technology Licensing at Cornell University as D-7364, developed
// through research conducted at Cornell University, and its associated copyrights
// solely for educational, research and non-profit purposes without fee is hereby
// granted, provided that the user agrees as follows:
//
// The permission granted herein is solely for the purpose of compiling the
// TowCrier source code. No other rights to use TownCrier and its associated
// copyrights for any other purpose are granted herein, whether commercial or
// non-commercial.
//
// Those desiring to incorporate TownCrier software into commercial products or use
// TownCrier and its associated copyrights for commercial purposes must contact the
// Center for Technology Licensing at Cornell University at 395 Pine Tree Road,
// Suite 310, Ithaca, NY 14850; email: ctl-connect@cornell.edu; Tel: 607-254-4698;
// FAX: 607-254-5454 for a commercial license.
//
// IN NO EVENT SHALL CORNELL UNIVERSITY BE LIABLE TO ANY PARTY FOR DIRECT,
// INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
// ARISING OUT OF THE USE OF TOWNCRIER AND ITS ASSOCIATED COPYRIGHTS, EVEN IF
// CORNELL UNIVERSITY MAY HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// THE WORK PROVIDED HEREIN IS ON AN "AS IS" BASIS, AND CORNELL UNIVERSITY HAS NO
// OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
// MODIFICATIONS.  CORNELL UNIVERSITY MAKES NO REPRESENTATIONS AND EXTENDS NO
// WARRANTIES OF ANY KIND, EITHER IMPLIED OR EXPRESS, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR
// PURPOSE, OR THAT THE USE OF TOWNCRIER AND ITS ASSOCIATED COPYRIGHTS WILL NOT
// INFRINGE ANY PATENT, TRADEMARK OR OTHER RIGHTS.
//
// TownCrier was developed with funding in part by the National Science Foundation
// (NSF grants CNS-1314857, CNS-1330599, CNS-1453634, CNS-1518765, CNS-1514261), a
// Packard Fellowship, a Sloan Fellowship, Google Faculty Research Awards, and a
// VMWare Research Award.
//

#include <jsonrpccpp/server/connectors/httpserver.h>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>

#include "sgx_uae_service.h"
#include "sgx_urts.h"

#include "Constants.h"
#include "Enclave_u.h"
#include "EthRPC.h"
#include "monitor.h"
#include "StatusRpcServer.h"
#include "attestation.h"
#include "bookkeeping/database.hxx"
#include "request-parser.hxx"
#include "stdint.h"
#include "utils.h"
#include "key-utils.h"
#include "tc-exception.hxx"

#define LOGURU_IMPLEMENTATION 1
#include "Log.h"

namespace po = boost::program_options;

extern ethRPCClient *rpc_client;
jsonrpc::HttpClient *httpclient;

std::atomic<bool> quit(false);
void exitGraceful(int) { quit.store(true); }

int main(int argc, const char *argv[]) {
  // init logging
  loguru::init(argc, argv);
  loguru::add_file("tc.log", loguru::Append, loguru::Verbosity_MAX);

  boost::filesystem::path current_path = boost::filesystem::current_path();

  bool options_rpc = false;
  bool options_daemon = false;
  string options_config = "config";

  try {
    po::options_description desc("Allowed options");
    desc.add_options()(
        "help,h", "print this message")(
        "rpc", po::bool_switch(&options_rpc)->default_value(false), "Launch RPC server")(
        "daemon,d", po::bool_switch(&options_daemon)->default_value(false), "Run TC as a daemon")(
        "config,c", po::value(&options_config)->default_value("config"), "Path to a config file");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help")) {
      cerr << desc << endl;
      return -1;
    }
    po::notify(vm);
  }
  catch (po::required_option &e) {
    cerr << e.what() << endl;
    return -1;
  } catch (std::exception &e) {
    cerr << e.what() << endl;
    return -1;
  } catch (...) {
    cerr << "Unknown error!" << endl;
    return -1;
  }

  // report the result of po parsing
  LL_INFO("input argument: rpc: %d", options_rpc);
  LL_INFO("input argument: daemon: %d", options_daemon);

  //!
  string working_dir;
  string pid_filename;
  int status_rpc_port;
  string sealed_sig_key;

  //! parse config files
  boost::property_tree::ptree pt;
  try {
    boost::property_tree::ini_parser::read_ini(options_config, pt);
    string st = pt.get<string>("RPC.RPChost");
    httpclient = new jsonrpc::HttpClient(st);
    rpc_client = new ethRPCClient(*httpclient);

    working_dir = pt.get<string>("daemon.working_dir");
    pid_filename = pt.get<string>("daemon.pid_file");
    status_rpc_port = pt.get<int>("status.port");
    sealed_sig_key = pt.get<string>("sealed.sig_key");

    LOG_F(INFO, "Using config file: %s", options_config.c_str());
    LOG_F(INFO, "cwd: %s", working_dir.c_str());
    LOG_F(INFO, "PID file: %s", pid_filename.c_str());
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
    exit(-1);
  }

  int ret;
  sgx_enclave_id_t eid;
  sgx_status_t st;

  //! register Ctrl-C handler
  std::signal(SIGINT, exitGraceful);

  jsonrpc::HttpServer status_server_connector(status_rpc_port);
  StatusRpcServer status_rpc_server(status_server_connector, eid);
  if (options_rpc) {
    status_rpc_server.StartListening();
    LOG_F(INFO, "RPC server started");
  }

  // create working dir if not existed
  boost::filesystem::create_directory(boost::filesystem::path(working_dir));

  if (options_daemon) {
    daemonize(working_dir, pid_filename);
  }

  const static string db_name = (boost::filesystem::path(working_dir) / "tc.db").string();
  LOG_F(INFO, "using db %s", db_name.c_str());
  bool create_db = false;
  if (boost::filesystem::exists(db_name) && !options_daemon) {
    std::cout << "Do you want to clean up the database? y/[n] ";
    std::string new_db;
    std::getline(std::cin, new_db);
    create_db = new_db == "y";
  } else {
    create_db = true;
  }
  LL_INFO("using new db: %d", create_db);
  OdbDriver driver(db_name, create_db);

  int nonce_offset = 0;

  ret = initialize_tc_enclave(&eid);
  if (ret != 0) {
    LOG_F(FATAL, "Failed to initialize the enclave");
    std::exit(-1);
  } else {
    LOG_F(INFO, "Enclave %lld created", eid);
  }

  string address;

  try {
    address = unseal_key(eid, sealed_sig_key);
    LL_INFO("using address %s", address.c_str());

    provision_key(eid, sealed_sig_key);
  }
  catch (const tc::EcallException& e) {
    LL_CRITICAL(e.what());
    exit(-1);
  }

  Monitor monitor(driver, eid, nonce_offset, quit);
  monitor.loop();

  if (options_rpc) {
    status_rpc_server.StopListening();
  }
  sgx_destroy_enclave(eid);
  delete rpc_client;
  delete httpclient;
  LOG_F(INFO, "all enclave closed successfully");
}
