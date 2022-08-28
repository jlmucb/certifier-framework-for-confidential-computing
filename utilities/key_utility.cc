#include <gflags/gflags.h>
#include "support.h"

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


DEFINE_bool(print_all, false,  "verbose");

DEFINE_bool(is_root, false,  "verbose");
DEFINE_string(key_name, "policyKey",  "key name");
DEFINE_string(key_type, "rsa-2048-private",  "policy key type");
DEFINE_string(authority_name, "policyAuthority",  "policy authority name");
DEFINE_string(key_output_file, "policy_key_file.bin",  "policy key file");
DEFINE_string(cert_output_file, "policy_cert_file.bin",  "policy cert file");
DEFINE_string(cert_output_file, "policy_cert_file.bin",  "policy cert file");
DEFINE_bool(generate_cert, false,  "generate cert?");


bool generate_key(const string& name, const string& type, 
      const string& authority, key_message* priv, key_message* pub) {

  int n = 0;
  if (type == "rsa-4096-private") {
    if (!make_certifier_rsa_key(4096,  priv)) {
      return false;
    }
  } else if (type == "rsa-2048-private") {
    if (!make_certifier_rsa_key(2048,  priv)) {
      return false;
    }
  } else if (type == "rsa-1024-private") {
    if (!make_certifier_rsa_key(1024,  priv)) {
      return false;
    }
  } else if (type == "ecc-384-private") {
    if (!make_certifier_ecc_key(384,  priv)) {
      return false;
    }
  } else {
    return false;
  }
  priv->set_key_name(name);
  priv->set_key_type(type);
  priv->set_key_format("vse-key");
  if (!private_key_to_public_key(*priv, pub))
    return false;

  return true;
}

bool generate_cert(const key_message& key, bool is_root, X509* cert) {
  return false;
}

int main(int an, char** av) {
  gflags::ParseCommandLineFlags(&an, &av, true);

  printf("key_utility.exe --key_type=rsa-2048-private --key_output_file=key_file.bin\n");
  printf(" --generate_cert=false, --cert_output_file=cert_file.bin\n");
  printf(" --authority_name=authority\n");
  printf("Key types : rsa-1024-private , rsa-2048-private, rsa-4096-private, ecc-384-private\n");

  key_message priv;
  key_message pub;
  key_message serialized_key;
  if (strcmp(FLAGS_key_type.c_str(), "rsa-1024-private") == 0 ||
      strcmp(FLAGS_key_type.c_str(), "rsa-2048-private") == 0 ||
      strcmp(FLAGS_key_type.c_str(), "rsa-4096-private") == 0 ||
      strcmp(FLAGS_key_type.c_str(), "ecc-384-private") == 0) {
    if (!generate_key(FLAGS_key_name, FLAGS_key_type, FLAGS_key_authority, &priv, &pub)) {
      printf("Couldn't generate key\n");
      return 0;
    }
  } else {
    printf("unsupported key type\n");
    return 1;
  }
  if (!priv->SerializeToString(&serialized_key)) {
    printf("Can't serialize key\n");
    return 1;
  }
  if (!write_file(FLAGS_key_output_file, serialized_key.size(), (byte*) serialized_key.data())) {
    printf("Can't write key file\n");
    return 1;
  }
  if (FLAGS_generate_cert) {
    string asn_cert;
    X509* cert= X509_new();
    if (!generate_cert(*priv, FLAGS_is_root, cert)) {
      printf("Can't generate cert\n");
      return 1;
    }
    if (!x509_to_asn1(cert, &ans_cert)) {
      printf("Can't convert to asn1\n");
      return 1;
    }
    if (!write_file(FLAGS_cert_output_file, asn_cert.size(),
          (byte*) asn_cert.data())) {
      printf("Can't write cert file\n");
      return 1;
    }
  }

  return 0;
}
