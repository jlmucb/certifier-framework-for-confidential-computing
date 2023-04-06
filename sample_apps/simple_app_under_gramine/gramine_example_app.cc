//  Copyright (c) 2021-22, VMware Inc, and the Certifier Authors.  All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * Attest/Verify/Seal/Unseal tests
 */

#include <gtest/gtest.h>
#include <gflags/gflags.h>

#include "gramine_api.h"

#define cert_file "gramine_example_app.crt"
#define BUF_SIZE 128
#define BUF_STORAGE_SIZE 4
#define TAG_SIZE 16
#define MAX_TAG_SIZE (BUF_STORAGE_SIZE + TAG_SIZE)
#define USER_DATA_SIZE 256
#define MAX_CERT_SIZE 2048

// operations are: cold-init, warm-restart, get-certifier, run-app-as-client, run-app-as-server
DEFINE_bool(print_all, false,  "verbose");
DEFINE_string(operation, "", "operation");

DEFINE_string(policy_host, "localhost", "address for policy server");
DEFINE_int32(policy_port, 8123, "port for policy server");
DEFINE_string(data_dir, "./app1_data/", "directory for application data");

DEFINE_string(server_app_host, "localhost", "address for app server");
DEFINE_int32(server_app_port, 8124, "port for server app server");

DEFINE_string(policy_store_file, "store.bin", "policy store file name");

// The test app performs five possible roles
//    cold-init: This creates application keys and initializes the policy store.
//    warm-restart:  This retrieves the policy store data.
//    get-certifier: This obtains the app admission cert naming the public app key from the service.
//    run-app-as-client: This runs the app as a server.
//    run-app-as-server: This runs the app as a client

#include "policy_key.cc"
//cc_trust_data* app_trust_data = nullptr;

// -----------------------------------------------------------------------------------------


byte cert[MAX_CERT_SIZE];

int main(int argc, char** argv) {
    int ret = 0;
    size_t len;
    int mr_size;
    int attestation_size;
    int sealed_size;
    int unsealed_size;

    bool status = false;
    byte attestation[MAX_ATTESTATION_SIZE];
    byte mr_recd[SGX_HASH_SIZE];

    byte buf[BUF_SIZE];
    byte enc_buf[BUF_SIZE + MAX_TAG_SIZE];
    byte dec_buf[BUF_SIZE];

    int cert_size = gramine_file_size(cert_file);

    if (cert_size < 0) {
        printf("Error reading file size for certificate\n");
        return false;
    }

    if (cert_size > MAX_CERT_SIZE) {
        printf("Certificate file too large\n");
        return false;
    }

    ret = gramine_rw_file(cert_file, cert, cert_size, false);
    if (ret < 0 && ret != -ENOENT) {
        printf("Can't read cert file\n");
        return false;
    }

    status = gramine_Init(ret, cert);
    if (status != true) {
        printf("gramine_Init failed\n");
	return 1;
    }

    printf("gramine_Attest/gramine_Verify test begin\n");

    /* Attest/Verify with SGX quote verification */
    byte user_data[USER_DATA_SIZE];
    for (int i = 0; i < USER_DATA_SIZE; i++) {
      user_data[i] = (byte)i;
    }

    status = gramine_Attest(USER_DATA_SIZE, user_data, &attestation_size, attestation);
    if (status != true) {
        printf("gramine_Attest failed\n");
	return 1;
    }

    status = gramine_Verify(USER_DATA_SIZE, user_data, attestation_size, attestation, &mr_size, mr_recd);
    if (status != true) {
        printf("gramine_Verify failed\n");
	return 1;
    }
    printf("gramine_Attest/gramine_Verify test successful\n");

    /* Sealing test with a small buffer */
    for (int i = 0; i < BUF_SIZE; i++) {
        buf[i] = (byte)i;
    }
    memset(enc_buf, 0, sizeof(enc_buf));
    memset(dec_buf, 0, sizeof(dec_buf));

    printf("gramine_Seal/gramine_Unseal test begin\n");

    status = gramine_Seal(BUF_SIZE, buf, &sealed_size, enc_buf);
    if (status != true) {
        printf("gramine_Seal failed\n");
	return 1;
    }

    status = gramine_Unseal(BUF_SIZE + MAX_TAG_SIZE, enc_buf, &unsealed_size, dec_buf);
    if (status != true) {
        printf("gramine_Unseal failed\n");
	return 1;
    }

    if (sealed_size != unsealed_size + MAX_TAG_SIZE) {
        printf("Gramine seal/unseal size failed\n");
	return 1;
    }

    ret = memcmp(buf, dec_buf, sizeof(dec_buf));
    if (ret) {
        printf("Gramine comparison of encrypted and decrypted buffers failed\n");
	return 1;
    }

#ifdef DEBUG
    printf("Testing seal interface - input buf:\n");
    gramine_print_bytes(BUF_SIZE, buf);
    printf("\n");
    printf("Testing seal interface - encrypted buf:\n");
    gramine_print_bytes(BUF_SIZE, enc_buf);
    printf("\n");
    printf("\n");
#endif

    printf("gramine_Seal/gramine_Unseal test successful\n");

exit:
    fflush(stdout);

    return ret;
}

#if 0
void client_application(secure_authenticated_channel& channel) {

  printf("Client peer id is %s\n", channel.peer_id_.c_str());

  // client sends a message over authenticated, encrypted channel
  const char* msg = "Hi from your secret client\n";
  channel.write(strlen(msg), (byte*)msg);

  // Get server response over authenticated, encrypted channel and print it
  string out;
  int n = channel.read(&out);
  printf("SSL client read: %s\n", out.data());
}


void server_application(secure_authenticated_channel& channel) {

  printf("Server peer id is %s\n", channel.peer_id_.c_str());

  // Read message from client over authenticated, encrypted channel
  string out;
  int n = channel.read(&out);
  printf("SSL server read: %s\n", (const char*) out.data());

  // Reply over authenticated, encrypted channel
  const char* msg = "Hi from your secret server\n";
  channel.write(strlen(msg), (byte*)msg);
}

bool run_me_as_server(const string& host_name, int port,
      string& asn1_policy_cert, key_message& private_key,
      string& private_key_cert) {

  printf("running as server\n");
  server_dispatch(host_name, port, asn1_policy_cert, private_key,
      private_key_cert, server_application);
  return true;
}

int main(int an, char** av) {
  gflags::ParseCommandLineFlags(&an, &av, true);
  an = 1;
  ::testing::InitGoogleTest(&an, av);

  if (FLAGS_operation == "") {
    printf("gramine_example_app.exe --print_all=true|false --operation=op --policy_host=policy-host-address --policy_port=policy-host-port\n");
    printf("\t --data_dir=-directory-for-app-data --server_app_host=my-server-host-address --server_app_port=server-host-port\n");
    printf("\t --policy_cert_file=self-signed-policy-cert-file-name --policy_store_file=policy-store-file-name\n");
    printf("\t --ark_cert_file=./service/milan_ark_cert.der --ask_cert_file=./service/milan_ask_cert.der --vcek_cert_file=./service/milan_vcek_cert.der\n");
    printf("Operations are: cold-init, warm-restart, get-certifier, run-app-as-client, run-app-as-server\n");
    return 0;
  }

  SSL_library_init();
  string enclave_type("gramine-enclave");
  string purpose("authentication");

  string store_file(FLAGS_data_dir);
  store_file.append(FLAGS_policy_store_file);
  app_trust_data = new cc_trust_data(enclave_type, purpose, store_file);
  if (app_trust_data == nullptr) {
    printf("couldn't initialize trust object\n");
    return 1;
  }

  // Init policy key info
  if (!app_trust_data->init_policy_key(initialized_cert_size, initialized_cert)) {
    printf("Can't init policy key\n");
    return 1;
  }

  // Init gramine enclave
  if (!app_trust_data->initialize_gramine_enclave_data(FLAGS_ark_cert_file,
        FLAGS_ask_cert_file, FLAGS_vcek_cert_file)) {
    printf("Can't init gramine enclave\n");
    return 1;
  }

  // Standard algorithms for the enclave
  string public_key_alg("rsa-2048");
  string symmetric_key_alg("aes-256");
  string hash_alg("sha-256");
  string hmac_alg("sha-256-hmac");

  // Carry out operation
  int ret = 0;
  if (FLAGS_operation == "cold-init") {
    if (!app_trust_data->cold_init(public_key_alg,
        symmetric_key_alg, hash_alg, hmac_alg)) {
      printf("cold-init failed\n");
      ret = 1;
    }
  } else if (FLAGS_operation == "warm-restart") {
    if (!app_trust_data->warm_restart()) {
      printf("warm-restart failed\n");
      ret = 1;
    }

  } else if (FLAGS_operation == "get-certifier") {
    if (!app_trust_data->certify_me(FLAGS_policy_host, FLAGS_policy_port)) {
      printf("certification failed\n");
      ret = 1;
    }
  } else if (FLAGS_operation == "run-app-as-client") {
    if (!app_trust_data->warm_restart()) {
      printf("warm-restart failed\n");
      ret = 1;
      goto done;
    }
    printf("running as client\n");
    if (!app_trust_data->cc_auth_key_initialized_ ||
        !app_trust_data->cc_policy_info_initialized_) {
      printf("trust data not initialized\n");
      ret = 1;
      goto done;
    }
    string my_role("client");
    secure_authenticated_channel channel(my_role);
    if (!channel.init_client_ssl(FLAGS_server_app_host, FLAGS_server_app_port,
          app_trust_data->serialized_policy_cert_,
          app_trust_data->private_auth_key_,
          app_trust_data->private_auth_key_.certificate())) {
      printf("Can't init client app\n");
      ret = 1;
      goto done;
    }

  // This is the actual application code.
  client_application(channel);
  } else if (FLAGS_operation == "run-app-as-server") {
    if (!app_trust_data->warm_restart()) {
      printf("warm-restart failed\n");
      ret = 1;
      goto done;
    }
    printf("running as server\n");
    server_dispatch(FLAGS_server_app_host, FLAGS_server_app_port,
        app_trust_data->serialized_policy_cert_,
          app_trust_data->private_auth_key_,
          app_trust_data->private_auth_key_.certificate(),
          server_application);
  } else {
    printf("Unknown operation\n");
  }

done:
  // app_trust_data->print_trust_data();
  app_trust_data->clear_sensitive_data();
  if (app_trust_data != nullptr) {
    delete app_trust_data;
  }
  return ret;
}
#endif