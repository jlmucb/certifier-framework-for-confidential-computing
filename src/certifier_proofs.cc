#include "support.h"
#include "certifier.h"
#include "simulated_enclave.h"
#include "application_enclave.h"
#include "attestation.h"
#include <sys/socket.h>
#include <netdb.h>


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


// Proof support
// -----------------------------------------------------------------------

predicate_dominance::predicate_dominance() {
  first_child_ = nullptr;
  next_ = nullptr;
}

predicate_dominance::~predicate_dominance() {
  predicate_dominance* current = first_child_;
  while (current != nullptr) {
    predicate_dominance* temp = current;
    current = current->next_;
    delete temp;
  }
  first_child_ = nullptr;
  next_ = nullptr;
}

predicate_dominance* predicate_dominance::find_node(const string& pred) {

  if (predicate_ ==  pred)
    return this;

  predicate_dominance* current = first_child_;
  predicate_dominance* t = nullptr;

  // breadth first search
  while (current != nullptr) {
    t = find_node(pred);
    if (t != nullptr)
      return t;
    current = current->next_;
  }

  // try children
  current = first_child_;
  while (current != nullptr) {
    t = current->find_node(pred);
    if (t != nullptr)
      return t;
    current = current->next_;
  }

  return nullptr;
}

// initial root must exist
bool predicate_dominance::insert(const string& parent, const string& descendant) {

  predicate_dominance* t = find_node(parent);
  if (t == nullptr)
    return false;
  if (dominates(*t, parent, descendant))
    return true;

  predicate_dominance* to_add = new(predicate_dominance);
  to_add->predicate_.assign(descendant);

  to_add->next_ = t->first_child_;
  t->first_child_= to_add;
  return true;
}

bool predicate_dominance::is_child(const string& descendant) {
  predicate_dominance* current = first_child_;

  while (current != nullptr) {
    if (current->predicate_ == descendant)
      return true;
    current = current->next_;
  }

  current = first_child_;
  while (current != nullptr) {
    if (current->is_child(descendant))
      return true;
    current = current->next_;
  }
  return false;
}

static void indent_spaces(int indent) {
  for (int i = 0; i < indent; i++)
    printf(" ");
}

void predicate_dominance::print_tree(int indent) {
  print_node(indent);
  print_descendants(indent + 2);
}

void predicate_dominance::print_node(int indent) {
  indent_spaces(indent);
  printf("Predicate: %s\n", predicate_.c_str());
}

void predicate_dominance::print_descendants(int indent) {
  predicate_dominance* current = first_child_;
  while (current != nullptr) {
    current->print_tree(indent);
    current = current->next_;
  }
}

bool dominates(predicate_dominance& root, const string& parent, const string& descendant) {
  if (parent == descendant)
    return true;
  predicate_dominance* pn = root.find_node(parent);
  if (pn == nullptr)
    return false;
  return pn->is_child(descendant);
}

//  -------------------------------------------------------------------------------------------

bool statement_already_proved(const vse_clause& cl, proved_statements* are_proved) {
 int n = are_proved->proved_size();
 for (int i = 0; i < n; i++) {
   const vse_clause& in_list = are_proved->proved(i);
   if (same_vse_claim(cl, in_list))
     return true;
  }
  return false;
}

bool verify_signed_assertion_and_extract_clause(const key_message& key,
      const signed_claim_message& sc, vse_clause* cl) {

  if (!sc.has_serialized_claim_message() || !sc.has_signing_key() ||
      !sc.has_signing_algorithm() || !sc.has_signature()) {
    return false;
  }

  // Deserialize claim to get clause
  string serialized_claim_string;
  claim_message asserted_claim;
  serialized_claim_string.assign((char*)sc.serialized_claim_message().data(),
        (int)sc.serialized_claim_message().size());
  if (!asserted_claim.ParseFromString(serialized_claim_string)) {
    printf("verify_signed_assertion_and_extract_clause: can't deserialize\n");
    return false;
  }

  if (!asserted_claim.has_claim_format()) {
    printf("verify_signed_assertion_and_extract_clause: no claim format\n");
    return false;
  }

  if (asserted_claim.claim_format() == "vse-clause") {

    string serialized_vse_string;
    vse_clause asserted_vse;
    serialized_vse_string.assign((char*)asserted_claim.serialized_claim().data(),
        (int)asserted_claim.serialized_claim().size());
    if (!asserted_vse.ParseFromString(serialized_vse_string)) {
      printf("verify_signed_assertion_and_extract_clause: can't deserialize vse\n");
      return false;
    }
    cl->CopyFrom(asserted_vse);
  } else {
    printf("verify_signed_assertion_and_extract_clause: only vse format supported\n");
    return false;
  }

  // verify signature
  return verify_signed_claim(sc, key);
}

bool add_fact_from_signed_claim(const signed_claim_message& signed_claim,
      proved_statements* already_proved) {

  const key_message& k = signed_claim.signing_key();
  vse_clause tcl;
  if (verify_signed_assertion_and_extract_clause(k, signed_claim, &tcl)) {
    if (tcl.verb() != "says" || tcl.subject().entity_type() != "key") {
      printf("Add_fact_from_signed_claim: bad subject or verb\n");
      print_vse_clause(tcl);
      printf("\n");
      return false;
    }
    if (!same_key(k, tcl.subject().key())) {
      printf("Add_fact_from_signed_claim: Different key\n");
      return false;
    }
    vse_clause* c = already_proved->add_proved();
    c->CopyFrom(tcl);
    return true;
  }
  return false;
}

bool get_vse_clause_from_signed_claim(const signed_claim_message& scm, vse_clause* c) {
  string serialized_cl;
  serialized_cl.assign((char*)scm.serialized_claim_message().data(), scm.serialized_claim_message().size());
  claim_message cm;
  if (!cm.ParseFromString(serialized_cl))
    return false;
  if (cm.claim_format() != "vse-clause")
    return false;

  string vse_cl_str;
  vse_cl_str.assign((char*)cm.serialized_claim().data(), cm.serialized_claim().size());
  vse_clause vse;
  if (!c->ParseFromString(vse_cl_str))
    return false;

  return true;
}

bool get_signed_measurement_claim_from_trusted_list(
        string& expected_measurement,
        signed_claim_sequence& trusted_measurements,
        signed_claim_message* claim) {

  for (int i= 0; i < trusted_measurements.claims_size(); i++) {
    vse_clause c;
    if (!get_vse_clause_from_signed_claim(trusted_measurements.claims(i), &c)) {
      continue;
    }
    string says_verb("says");
    string it_verb("is-trusted");
    if (c.verb() != says_verb) {
      continue;
    }
    // policy-key says measurement is-trusted
    if (!c.has_clause() || c.verb() != says_verb) {
      continue;
    }
    if (c.clause().verb() != it_verb || !c.clause().has_subject()) {
      continue;
    }
    if (c.clause().subject().entity_type() != "measurement") {
      continue;
    }

#ifdef DEBUG
    printf("\n got measurement size: %ld\n", c.clause().subject().measurement().size());
    print_bytes(c.clause().subject().measurement().size(), (byte*)c.clause().subject().measurement().data());
    printf("\n expected measurement size: %ld\n", expected_measurement.size());
    print_bytes(expected_measurement.size(), (byte*) expected_measurement.data());
    printf("\n");
#endif

    if (memcmp(c.clause().subject().measurement().data(),
            (byte*) expected_measurement.data(), expected_measurement.size()) == 0) {
      claim->CopyFrom(trusted_measurements.claims(i));
      return true;
    }
  }
  return false;
}

bool get_signed_platform_claim_from_trusted_list(
        const key_message& expected_key,
        signed_claim_sequence& trusted_platforms,
        signed_claim_message* claim) {

  for (int i= 0; i < trusted_platforms.claims_size(); i++) {
    vse_clause c;
    if (!get_vse_clause_from_signed_claim(trusted_platforms.claims(i), &c)) {
      continue;
    }
    string says_verb("says");
    string it1_verb("is-trusted");
    string it2_verb("is-trusted-for-attestation");
    // policy-key says platform-key is-trusted-for-attestation
    if (c.verb() != says_verb)
      continue;
    if (!c.has_clause() || !c.clause().has_verb())
      continue;
    if ((c.clause().verb() != it1_verb && c.clause().verb() != it2_verb) ||
          !c.clause().has_subject())
      continue;
    if (c.clause().subject().entity_type() != "key")
      continue;
    if (same_key(c.clause().subject().key(), expected_key)) {
      claim->CopyFrom(trusted_platforms.claims(i));
      return true;
    }
  }
  return false;
}

// Statement construction support
// -------------------------------------------------------------------------

bool construct_vse_attestation_statement(const key_message& attest_key,
        const key_message& enclave_key, const string& measurement,
        vse_clause* vse_attest_clause) {
  string s1("says");
  string s2("speaks-for");

  entity_message measurement_entity;
  entity_message attest_key_entity;
  entity_message enclave_key_entity;
  if (!make_key_entity(attest_key, &attest_key_entity))
    return false;
  if (!make_key_entity(enclave_key, &enclave_key_entity))
    return false;
  if (!make_measurement_entity(measurement, &measurement_entity))
    return false;

  vse_clause auth_key_speaks_for_measurement;
  if (!make_simple_vse_clause(enclave_key_entity, s2, measurement_entity, &auth_key_speaks_for_measurement)) {
    printf("Construct attestation error 1\n");
    return false;
  }
  if (!make_indirect_vse_clause(attest_key_entity, s1, auth_key_speaks_for_measurement, vse_attest_clause)) {
    printf("Construct attestation error 1\n");
    return false;
  }
  return true;
}

bool make_attestation_user_data(const string& enclave_type,
         const key_message& enclave_key, attestation_user_data* out) {

  out->set_enclave_type(enclave_type);
  time_point t_now;
  if (!time_now(&t_now))
    return false;
  string time_str;
  if (!time_to_string(t_now, &time_str))
    return false;
  out->set_time(time_str);
  out->mutable_enclave_key()->CopyFrom(enclave_key);
  return true;
}

bool construct_what_to_say(string& enclave_type,
      key_message& enclave_pk, string* what_to_say) {

  if (enclave_type != "simulated-enclave" && enclave_type != "application-enclave" &&
      enclave_type != "sev-enclave" && enclave_type != "oe-enclave" &&
      enclave_type != "asylo-enclave" && enclave_type != "gramine-enclave")
    return false;

  attestation_user_data ud;
  if (!make_attestation_user_data(enclave_type, enclave_pk, &ud)) {
    printf("make_attestation_user_data failed\n");
    return false;
  }
  if (!ud.SerializeToString(what_to_say)) {
    printf("ud.SerializeToString failed\n");
    return false;
  }

  return true;
}

// type is usually "vse-attestation-report"
bool sign_report(const string& type, const string& to_be_signed, const string& signing_alg,
      const key_message& signing_key, string* serialized_signed_report) {

  signed_report report;
  key_message public_signing_alg;
  if (!private_key_to_public_key(signing_key, &public_signing_alg)) {
    printf("private_key_to_public_key failed\n");
    return false;
  }

  report.set_report_format("vse-attestation-report");
  report.set_signing_algorithm(signing_alg);
  report.mutable_signing_key()->CopyFrom(public_signing_alg);
  report.set_report(to_be_signed);

  int size = cipher_block_byte_size(signing_alg.c_str());
  if (size < 0) {
    printf("Bad cipher\n");
    return false;
  }

  byte signature[size];
  if (signing_alg == "rsa-2048-sha256-pkcs-sign") {
    if (signing_key.key_type() != "rsa-2048-private") {
      printf("Wrong key\n");
      return false;
    }
    RSA* rsa_key = RSA_new();
    if (!key_to_RSA(signing_key, rsa_key)) {
      printf("key_to_RSA failed\n");
      return false;
    }
    if (!rsa_sign("sha-256", rsa_key, to_be_signed.size(), (byte*)to_be_signed.data(),
            &size, signature)) {
      printf("rsa_sign failed\n");
      RSA_free(rsa_key);
      return false;
    }
    RSA_free(rsa_key);
  } else if (signing_alg == "rsa-4096-sha384-pkcs-sign") {
    if (signing_key.key_type() != "rsa-4096-private") {
      printf("Wrong key\n");
      return false;
    }
    RSA* rsa_key = RSA_new();
    if (!key_to_RSA(signing_key, rsa_key)) {
      printf("key_to_RSA failed\n");
      return false;
    }
    if (!rsa_sign("sha-384", rsa_key, to_be_signed.size(), (byte*)to_be_signed.data(),
            &size, signature)) {
      printf("rsa_sign failed\n");
      RSA_free(rsa_key);
      return false;
    }
    RSA_free(rsa_key);
  } else if (signing_alg == "ecc-384-sha384-pkcs-sign") {
    if (signing_key.key_type() != "ecc-384-private") {
      printf("Wrong key\n");
      return false;
    }
    EC_KEY* ecc_key = key_to_ECC(signing_key);
    if (ecc_key == nullptr) {
      printf("key_to_ECC failed\n");
      return false;
    }
    if (!ecc_sign("sha-384", ecc_key, to_be_signed.size(), (byte*)to_be_signed.data(),
            &size, signature)) {
      printf("ecc_sign failed\n");
      EC_KEY_free(ecc_key);
      return false;
    }
    EC_KEY_free(ecc_key);
  } else {
    return false;
  }

  report.set_signature((byte*)signature, size);
  if (!report.SerializeToString(serialized_signed_report)) {
    printf("Can't serialize report\n");
    return false;
  }
  return true;
}

// type is usually "signed-vse-attestation-report"
bool verify_report(string& type, string& serialized_signed_report,
      const key_message& signer_key) {

  signed_report sr;
  if (!sr.ParseFromString(serialized_signed_report)) {
    printf("Can't parse serialized_signed_report\n");
    return false;
  }

  if (sr.report_format() != "vse-attestation-report") {
    printf("Format should be vse-attestation-report\n");
    return false;
  }

  bool success = false;
  if (sr.signing_algorithm() == "rsa-2048-sha256-pkcs-sign") {
    RSA* rsa_key = RSA_new();
    if (!key_to_RSA(signer_key, rsa_key)) {
      printf("key_to_RSA failed\n");
      return false;
    }
    int size = sr.signature().size();
    success = rsa_verify("sha-256", rsa_key, sr.report().size(),
            (byte*)sr.report().data(),
            size, (byte*)sr.signature().data());
    RSA_free(rsa_key);
  } else if (sr.signing_algorithm() == "rsa-4096-sha384-pkcs-sign") {
    RSA* rsa_key = RSA_new();
    if (!key_to_RSA(signer_key, rsa_key)) {
      printf("key_to_RSA failed\n");
      return false;
    }
    int size = sr.signature().size();
    success = rsa_verify("sha-384", rsa_key, sr.report().size(),
            (byte*)sr.report().data(),
            size, (byte*)sr.signature().data());
    RSA_free(rsa_key);
  } else if (sr.signing_algorithm() == "ecc-384-sha384-pkcs-sign") {
    EC_KEY* ecc_key = key_to_ECC(signer_key);
    if (ecc_key == nullptr) {
      printf("key_to_RSA failed\n");
      return false;
    }
    int size = sr.signature().size();
    success = ecc_verify("sha-384", ecc_key, sr.report().size(),
            (byte*)sr.report().data(),
            size, (byte*)sr.signature().data());
    EC_KEY_free(ecc_key);
  } else {
    printf("Unsupported algorithm\n");
    return false;
  }

  if (!success) {
    printf("report verify returning false\n");
  }

  return success;
}

//  -------------------------------------------------------------------------------------------

/*
  Certifier proofs

  Rules
    rule 1 (R1): If measurement is-trusted and key1 speaks-for measurement then
        key1 is-trusted-for-authentication.
    rule 2 (R2): If key2 speaks-for key1 and key3 speaks-for key2 then key3 speaks-for key1
    rule 3 (R3): If key1 is-trusted and key1 says X, then X is true
    rule 4 (R4): If key2 speaks-for key1 and key1 is-trusted then key2 is-trusted
    rule 5 (R5): If key1 is-trustedXXX and key1 says key2 is-trustedYYY then key2 is-trustedYYY
          provided is-trustedXXX dominates is-trustedYYY
    rule 6 (R6): if key1 is-trustedXXX and key1 says key2 speaks-for measurement then
        key2 speaks-for measurement
          provided is-trustedXXX dominates is-trusted-for-attestation
    rule 7 (R1): If measurement is-trusted and key1 speaks-for measurement then
        key1 is-trusted-for-attestation.

  A statement, X, signed by entity1 is the same as entity1 says X

  Axioms
    axiom 1 (A1): policy-key is-trusted
 */

bool init_certifier_rules(certifier_rules& rules) {
  string* r1 =  rules.add_rule();
  string* r2 =  rules.add_rule();
  string* r3 =  rules.add_rule();
  string* r4 =  rules.add_rule();
  string* r5 =  rules.add_rule();
  string* r6 =  rules.add_rule();
  string* r7 =  rules.add_rule();
  r1->assign("If measurement is-trusted and key1 speaks-for measurement then key1 is-trusted-for-authentication.");
  r2->assign("If key2 speaks-for key1 and key3 speaks-for key2 then key3 speaks-for key1.");
  r3->assign("If key1 is-trusted and key1 says X, then X is true.");
  r4->assign("If key2 speaks-for key1 and key1 is-trusted then key2 is-trusted.");
  r5->assign("If key1 is-trusted-for-a-purpose and key1 says key2 is-trusted-for-another-purpose then key2 is-trusted-for-another-purpose, if is-trusted-for-a-purpose dominate is-trusted-for-another-purpose.");
  r6->assign("If key1 is-trusted-for-a-attestation and key1 says key2 speaks-for measurement then key2 speaks-for measurment.");
  r7->assign("If measurement is-trusted and key2 speaks-for measurement then key2 is-trusted-for-attestation.");
  return true;
}

static const int num_is_trusted_kids = 2;
static const char* kids[2] = {
  "is-trusted-for-attestation",
  "is-trusted-for-authentication",
};
bool init_dominance_tree(predicate_dominance& root) {
  root.predicate_.assign("is-trusted");

  string descendant;
  for (int i = 0; i < num_is_trusted_kids; i++) {
    descendant.assign(kids[i]);
    if (!root.insert(root.predicate_, descendant))
      return false;
  }

  return true;
}

// policy
//    byte 0
//      bit     value
//      0       debug disallowed when set
//      1       key sharing is disallowed when setA
//      3       can't migrate when set
//    byte 1: API_MAJOR
//    byte 2: API_MINOR
bool get_migrate_property(const sev_attestation_message& sev_att, property* prop) {
  string str_value;

  attestation_report* r= (attestation_report*) sev_att.reported_attestation().data();
  if ((r->policy&0x4ULL))
    str_value = "no";
  else
    str_value = "yes";
  string str_name("migrate");
  string str_equal("=");
  string str_type("string");
  return make_property(str_name, str_type, str_equal, 0, str_value, prop);
}

bool get_key_share_property(const sev_attestation_message& sev_att, property* prop) {
  string str_value;

  attestation_report* r= (attestation_report*) sev_att.reported_attestation().data();
  if ((r->policy&0x2ULL))
    str_value = "no";
  else
    str_value = "yes";
  string str_name("key-share");
  string str_equal("=");
  string str_type("string");
  return make_property(str_name, str_type, str_equal, 0, str_value, prop);
}

bool get_debug_property(const sev_attestation_message& sev_att, property* prop) {
  string str_value;

  attestation_report* r= (attestation_report*) sev_att.reported_attestation().data();
  if ((r->policy&0x1ULL))
    str_value = "no";
  else
    str_value = "yes";
  string str_name("debug");
  string str_equal("=");
  string str_type("string");
  return make_property(str_name, str_type, str_equal, 0, str_value, prop);
}

bool get_major_api_property(const sev_attestation_message& sev_att, property* prop) {
  int value = 0;

  attestation_report* r= (attestation_report*) sev_att.reported_attestation().data();
  value = (int)(((r->policy)>>8)&0xff);
  string str_name("api_major");
  string str_equal("=");
  string str_type("int");
  return make_property(str_name, str_type, str_equal, value, str_name, prop);
}

bool get_minor_api_property(const sev_attestation_message& sev_att, property* prop) {
  int value = 0;

  attestation_report* r= (attestation_report*) sev_att.reported_attestation().data();
  value = (int)(((r->policy)>>16)&0xff);
  string str_name("api_minor");
  string str_equal("=");
  string str_type("int");
  return make_property(str_name, str_type, str_equal, value, str_name, prop);
}

bool add_vse_proved_statements_from_sev_attest(const sev_attestation_message& sev_att,
          const key_message& vcek_key,
          proved_statements* already_proved) {

  properties props;
  {
    property p1;
    if  (get_migrate_property(sev_att, &p1)) {
      props.add_props()->CopyFrom(p1);
    }
  }
  {
    property p1;
    if  (get_debug_property(sev_att, &p1)) {
      props.add_props()->CopyFrom(p1);
    }
  }
  {
    property p1;
    if  (get_key_share_property(sev_att, &p1)) {
      props.add_props()->CopyFrom(p1);
    }
  }
  {
    property p1;
    if  (get_major_api_property(sev_att, &p1)) {
      props.add_props()->CopyFrom(p1);
    }
  }
  {
    property p1;
    if  (get_minor_api_property(sev_att, &p1)) {
      props.add_props()->CopyFrom(p1);
    }
  }

  attestation_user_data ud;
  if (!ud.ParseFromString(sev_att.what_was_said())) {
    return false;
  }

  attestation_report* r= (attestation_report*) sev_att.reported_attestation().data();

  string m_str;
  m_str.assign((char*)r->measurement, 48);
  entity_message m_ent;
  if (!make_measurement_entity(m_str, &m_ent)) {
    printf("add_vse_proved_statements_from_sev_attest: Can't make measurement entity\n");
    return false;
  }

  entity_message auth_ent;
  if (!make_key_entity(ud.enclave_key(), &auth_ent)) {
    printf("add_vse_proved_statements_from_sev_attest: Can't make measurement entity\n");
    return false;
  }

  platform current_platform;
  string type("amd-sev-snp");
  if (!make_platform(type, props, &vcek_key, &current_platform)) {
    printf("add_vse_proved_statements_from_sev_attest: Can't make platform\n");
    return false;
  }

  environment env;
  entity_message env_ent;
  if (!make_environment(current_platform, m_str, &env)) {
    printf("add_vse_proved_statements_from_sev_attest: Can't make environment\n");
    return false;
  }
  if (!make_environment_entity(env, &env_ent)) {
    printf("add_vse_proved_statements_from_sev_attest: Can't make environment entity\n");
    return false;
  }

  string says_verb("says");
  string speaks_verb("speaks-for");
  string is_env_verb("is-environment");
  vse_clause* cl1 = already_proved->add_proved();
  if (!make_unary_vse_clause(env_ent, is_env_verb, cl1)) {
    printf("add_vse_proved_statements_from_sev_attest: can't make environment clause\n");
    return false;
  }

  vse_clause c1;
  if (!make_simple_vse_clause(auth_ent, speaks_verb, env_ent, &c1)) {
    printf("add_vse_proved_statements_from_sev_attest: Can't make speaks-for clause\n");
    return false;
  }

  vse_clause* cl2 = already_proved->add_proved();
  entity_message vcek_ent;
  if (!make_key_entity(vcek_key, &vcek_ent)) {
    printf("add_vse_proved_statements_from_sev_attest: Can't make vcek entity\n");
    return false;
  }
  if (!make_indirect_vse_clause(vcek_ent, says_verb, c1, cl2)) {
    printf("add_vse_proved_statements_from_sev_attest: Can't make says speaks-for\n");
    return false;
  }
  return true;
}

bool init_axiom(key_message& pk, proved_statements* are_proved) {
  // Add axiom pk is-trusted
  entity_message policy_key_entity;
  vse_clause axiom;
  if (!make_key_entity(pk, &policy_key_entity))
    return false;
  string is_trusted_verb("is-trusted");
  if (!make_unary_vse_clause(policy_key_entity, is_trusted_verb, &axiom))
    return false;
  vse_clause* to_insert = are_proved->add_proved();
  to_insert->CopyFrom(axiom);
  return true;
}

const int max_key_depth = 30;
const int max_measurement_size = 512;
const int max_user_data_size = 4096;

bool init_proved_statements(key_message& pk, evidence_package& evp,
      proved_statements* already_proved) {

  cert_keys_seen_list seen_keys_list(max_key_depth);
  // verify already signed assertions, converting to vse_clause
  int nsa = evp.fact_assertion_size();
  for (int i = 0; i < nsa; i++) {
    if (evp.fact_assertion(i).evidence_type() == "signed-claim") {
      signed_claim_message sc;
      string t_str;
      t_str.assign((char*)evp.fact_assertion(i).serialized_evidence().data(),
          evp.fact_assertion(i).serialized_evidence().size());
      if (!sc.ParseFromString(t_str))
        return false;

      vse_clause to_add;
      const key_message& km= sc.signing_key();

      if (!verify_signed_assertion_and_extract_clause(km, sc, &to_add)) {
        printf("signed claim %d failed\n", i);
        return false;
      }
      // We can only add Key says statements and we must make
      // sure the subject of says is the signing key
      if (!to_add.has_subject() || !to_add.has_verb() || to_add.verb() != "says")
        return false;
      if (to_add.subject().entity_type() != "key")
        return false;
      const key_message& ks = to_add.subject().key();
      if (!same_key(km, ks)) {
        // wrong key signed message
        printf("Wrong key signed message\n");
        return false;
      }
      vse_clause* cl_to_insert = already_proved->add_proved();
      cl_to_insert->CopyFrom(to_add);
#ifdef OE_CERTIFIER
    } else if (evp.fact_assertion(i).evidence_type() == "oe-attestation-report") {
      size_t user_data_size = max_user_data_size;
      byte user_data[user_data_size];
      size_t measurement_out_size = max_measurement_size;
      byte measurement_out[measurement_out_size];

      if (!oe_Verify((byte *)evp.fact_assertion(i).serialized_evidence().data(),
                     evp.fact_assertion(i).serialized_evidence().size(),
                     user_data, &user_data_size, measurement_out, &measurement_out_size)) {
        printf("init_proved_statements: oe_Verify failed\n");
        return false;
      }

      // user_data should be a attestation_user_data
      string ud_str;
      ud_str.assign((char*)user_data, user_data_size);
      attestation_user_data ud;
      if (!ud.ParseFromString(ud_str))
        return false;

      // construct vse-clause (key speaks-for measurement)
      entity_message* key_ent = new(entity_message);
      if (!make_key_entity(ud.enclave_key(), key_ent)) {
        printf("init_proved_statements: make_key_entity failed\n");
        return false;
      }
      entity_message* measurement_ent = new(entity_message);
      string m;
      m.assign((char*)measurement_out, measurement_out_size);
      if (!make_measurement_entity(m, measurement_ent)) {
        printf("init_proved_statements: make_measurement_entity failed\n");
        return false;
      }
      vse_clause* cl_to_insert = already_proved->add_proved();
      string sf("speaks-for");
      if (!make_simple_vse_clause(*key_ent, sf, *measurement_ent, cl_to_insert)) {
        printf("init_proved_statements: make_simple_vse_clause failed\n");
        return false;
      }
#endif
#ifdef ASYLO_CERTIFIER
    } else if (evp.fact_assertion(i).evidence_type() == "asylo-evidence") {
      int user_data_size = max_user_data_size;
      byte user_data[user_data_size];
      int measurement_out_size = max_measurement_size;
      byte measurement_out[measurement_out_size];
#ifdef DEBUG
      printf("init_proved_statements: trying asylo_Verify\n");
#endif

      string pk_str = pk.SerializeAsString();
#ifdef DEBUG
      printf("init_proved_statements: print pk\n");
      print_bytes(pk_str.size(), (byte*)pk_str.c_str());

      printf("init_proved_statements: print evp\n");
      print_bytes(evp.fact_assertion(i).serialized_evidence().size(),
       (byte *)evp.fact_assertion(i).serialized_evidence().data());
#endif

      if (!asylo_Verify(
           evp.fact_assertion(i).serialized_evidence().size(),
           (byte *)evp.fact_assertion(i).serialized_evidence().data(),
           &user_data_size, user_data, &measurement_out_size,
           measurement_out)) {
        printf("init_proved_statements: asylo_Verify failed\n");
      }

#ifdef DEBUG
      printf("\nasylo returned user data: size: %d\n", user_data_size);
      print_bytes(user_data_size, user_data);
      printf("\nasylo returned measurement: size: %d\n", measurement_out_size);
      print_bytes(measurement_out_size, measurement_out);
#endif

      // user_data should be a attestation_user_data
      string ud_str;
      ud_str.assign((char*)user_data, user_data_size);
      attestation_user_data ud;
      if (!ud.ParseFromString(ud_str))
        return false;

      entity_message* key_ent = new(entity_message);
      if (!make_key_entity(ud.enclave_key(), key_ent)) {
        printf("init_proved_statements: make_key_entity failed\n");
        return false;
      }
      entity_message* measurement_ent = new(entity_message);
      string m;
      m.assign((char*)measurement_out, measurement_out_size);
      if (!make_measurement_entity(m, measurement_ent)) {
        printf("init_proved_statements: make_measurement_entity failed\n");
        return false;
      }
      vse_clause* cl_to_insert = already_proved->add_proved();
      string sf("speaks-for");
      if (!make_simple_vse_clause(*key_ent, sf, *measurement_ent, cl_to_insert)) {
        printf("init_proved_statements: make_simple_vse_clause failed\n");
        return false;
      }
#endif  // ASYLO
#ifdef GRAMINE_CERTIFIER
    } else if (evp.fact_assertion(i).evidence_type() == "gramine-evidence") {
      int user_data_size = 4096;
      byte user_data[user_data_size];
      int measurement_out_size = 256;
      byte measurement_out[measurement_out_size];
#ifdef DEBUG
      printf("init_proved_statements: trying gramine_Verify\n");
#endif

      string pk_str = pk.SerializeAsString();
#ifdef DEBUG
      printf("init_proved_statements: print pk\n");
      print_bytes(pk_str.size(), (byte*)pk_str.c_str());

      printf("init_proved_statements: print evp\n");
      print_bytes(evp.fact_assertion(i).serialized_evidence().size(),
       (byte *)evp.fact_assertion(i).serialized_evidence().data());
#endif

      if (!gramine_Verify(
           evp.fact_assertion(i).serialized_evidence().size(),
           (byte *)evp.fact_assertion(i).serialized_evidence().data(),
           &user_data_size, user_data, &measurement_out_size,
           measurement_out)) {
        printf("init_proved_statements: gramine_Verify failed\n");
      }

#ifdef DEBUG
      printf("\ngramine returned user data: size: %d\n", user_data_size);
      print_bytes(user_data_size, user_data);
      printf("\ngramine returned measurement: size: %d\n", measurement_out_size);
      print_bytes(measurement_out_size, measurement_out);
#endif

      // user_data should be a attestation_user_data
      string ud_str;
      ud_str.assign((char*)user_data, user_data_size);
      attestation_user_data ud;
      if (!ud.ParseFromString(ud_str))
        return false;

      entity_message* key_ent = new(entity_message);
      if (!make_key_entity(ud.enclave_key(), key_ent)) {
        printf("init_proved_statements: make_key_entity failed\n");
        return false;
      }
      entity_message* measurement_ent = new(entity_message);
      string m;
      m.assign((char*)measurement_out, measurement_out_size);
      if (!make_measurement_entity(m, measurement_ent)) {
        printf("init_proved_statements: make_measurement_entity failed\n");
        return false;
      }
      vse_clause* cl_to_insert = already_proved->add_proved();
      string sf("speaks-for");
      if (!make_simple_vse_clause(*key_ent, sf, *measurement_ent, cl_to_insert)) {
        printf("init_proved_statements: make_simple_vse_clause failed\n");
        return false;
      }
#endif  // GRAMINE_CERTIFIER
    } else if (evp.fact_assertion(i).evidence_type() == "cert") {
      // A cert always means "the signing-key says the subject-key is-trusted-for-attestation"
      // construct vse statement.

      // This whole thing is more complicated because we have to keep track of
      // previously seen subject keys which, as issuer keys, will sign other
      // keys.  The only time we can get the issuer_key directly is when the cert
      // is self signed.

      X509* x = X509_new();
      if (x == nullptr)
        return false;
      if (!asn1_to_x509(evp.fact_assertion(i).serialized_evidence(), x)) {
        printf("init_proved_statements: Can't asn convert cert\n");
        return false;
      }

      key_message* subject_key = new key_message;
      if (!x509_to_public_key(x, subject_key)) {
        printf("init_proved_statements: Can't convert subject key to key\n");
        return false;
      }
      if (!seen_keys_list.add_key_seen(subject_key)) {
        printf("init_proved_statements: Can't add subject key to seen keys\n");
        return false;
      }

      const key_message* signer_key = get_issuer_key(x, seen_keys_list);
      if (signer_key == nullptr) {
        printf("init_proved_statements: Can't find issuer key\n");
        return false;
      }
      EVP_PKEY* signer_pkey = pkey_from_key(*signer_key);
      if (signer_pkey == nullptr) {
        printf("init_proved_statements: Can't get pkey\n");
        return false;
      }
      bool success = (X509_verify(x, signer_pkey) == 1);
      if (success) {
        // add to proved: signing-key says subject-key is-trusted-for-attestation
        vse_clause* cl = already_proved->add_proved();
        if (!construct_vse_attestation_from_cert(*subject_key, *signer_key, cl)) {
          printf("init_proved_statements: Can't construct vse attestation from cert\n");
          return false;
        }
      }

      // Todo: free on errors too
      if (signer_pkey != nullptr) {
        EVP_PKEY_free(signer_pkey);
        signer_pkey = nullptr;
      }
      if (x != nullptr) {
        X509_free(x);
        x = nullptr;
      }
#ifdef SEV_SNP
    } else if (evp.fact_assertion(i).evidence_type() == "sev-attestation-with-platform") {
      string t_str;
      t_str.assign((char*)evp.fact_assertion(i).serialized_evidence().data(),
          evp.fact_assertion(i).serialized_evidence().size());
      sev_attestation_message sev_att;
      if (!sev_att.ParseFromString(evp.fact_assertion(i).serialized_evidence())) {
        printf("sev attest processing, error 1\n");
        return false;
      }

      // vcekKey
      // Last proved statement should have been ask_key says vcek_key is-trusted-for-attestation;
      if (already_proved->proved_size() < 1) {
        printf("sev attest processing, error 2\n");
        return false;
      }
      const vse_clause& last_clause = already_proved->proved(already_proved->proved_size() - 1);
      if (!last_clause.has_clause()) {
        printf("sev attest processing, error 3\n");
        return false;
      }
      if (!last_clause.clause().has_subject() || last_clause.clause().subject().entity_type() != "key") {
        printf("sev attest processing, error 4\n");
        return false;
      }
      const key_message& vcek_key = last_clause.clause().subject().key();

      EVP_PKEY* verify_pkey = pkey_from_key(vcek_key);
      if (verify_pkey == nullptr) {
        printf("sev attest processing, error 5\n");
        return false;
      }

#if 0
      int size_measurement = max_measurement_size;
      byte measurement[size_measurement];
      extern bool verify_sev_Attest(EVP_PKEY* key, int size_sev_attestation, byte* the_attestation,
          int* size_measurement, byte* measurement);
      bool success = verify_sev_Attest(verify_pkey, evp.fact_assertion(i).serialized_evidence().size(),
            (byte*)evp.fact_assertion(i).serialized_evidence().data(), &size_measurement, measurement);
      EVP_PKEY_free(verify_pkey);
      verify_pkey = nullptr;

      if (!success) {
        printf("Verify failed\n");
        return false;
      }
#else
      int size_measurement = 48;
      byte measurement[size_measurement];
      memset(measurement, 0, size_measurement);
#endif
      if (!add_vse_proved_statements_from_sev_attest(sev_att, vcek_key,
            already_proved)) {
        printf("init_proved_statements: can't dd_vse_proved_statements_from_sev_attest\n");
        return false;
      }
    } else if (evp.fact_assertion(i).evidence_type() == "sev-attestation") {
      string t_str;
      t_str.assign((char*)evp.fact_assertion(i).serialized_evidence().data(),
          evp.fact_assertion(i).serialized_evidence().size());
      sev_attestation_message sev_att;
      if (!sev_att.ParseFromString(evp.fact_assertion(i).serialized_evidence())) {
        printf("sev attest processing, error 1\n");
        return false;
      }

      // vcekKey
      // Last proved statement should have been ask_key says vcek_key is-trusted-for-attestation;
      if (already_proved->proved_size() < 1) {
        printf("sev attest processing, error 2\n");
        return false;
      }
      const vse_clause& last_clause = already_proved->proved(already_proved->proved_size() - 1);
      if (!last_clause.has_clause()) {
        printf("sev attest processing, error 3\n");
        return false;
      }
      if (!last_clause.clause().has_subject() || last_clause.clause().subject().entity_type() != "key") {
        printf("sev attest processing, error 4\n");
        return false;
      }
      const key_message& vcek_key = last_clause.clause().subject().key();

      EVP_PKEY* verify_pkey = pkey_from_key(vcek_key);
      if (verify_pkey == nullptr) {
        printf("sev attest processing, error 5\n");
        return false;
      }

      int size_measurement = max_measurement_size;
      byte measurement[size_measurement];
      extern bool verify_sev_Attest(EVP_PKEY* key, int size_sev_attestation, byte* the_attestation,
          int* size_measurement, byte* measurement);
      bool success = verify_sev_Attest(verify_pkey, evp.fact_assertion(i).serialized_evidence().size(),
            (byte*)evp.fact_assertion(i).serialized_evidence().data(), &size_measurement, measurement);
      EVP_PKEY_free(verify_pkey);
      verify_pkey = nullptr;

      if (!success) {
        printf("Verify failed\n");
        return false;
      }

      attestation_user_data ud;
      if (!ud.ParseFromString(sev_att.what_was_said())) {
        return false;
      }
      string says_verb("says");
      string speaks_verb("speaks-for");
      string m_str;
      m_str.assign((char*)measurement, size_measurement);
      entity_message m_ent;
      if (!make_measurement_entity(m_str, &m_ent)) {
        printf("sev attest processing, error 7\n");
        return false;
      }

      entity_message auth_ent;
      if (!make_key_entity(ud.enclave_key(), &auth_ent)) {
        printf("sev attest processing, error 8\n");
        return false;
      }

      vse_clause c1;
      if (!make_simple_vse_clause(auth_ent, speaks_verb, m_ent, &c1)) {
        printf("sev attest processing, error 9\n");
        return false;
      }

      // vcekKey says authKey speaks-for measurement
      entity_message vcek_ent;
      if (!make_key_entity(vcek_key, &vcek_ent)) {
        printf("sev attest processing, error 10\n");
        return false;
      }
      vse_clause* cl = already_proved->add_proved();
      if (!make_indirect_vse_clause(vcek_ent, says_verb, c1, cl)) {
        printf("sev attest processing, error 11\n");
        return false;
      }
#endif
    } else if (evp.fact_assertion(i).evidence_type() == "signed-vse-attestation-report") {
      string t_str;
      t_str.assign((char*)evp.fact_assertion(i).serialized_evidence().data(),
          evp.fact_assertion(i).serialized_evidence().size());
      string type("vse-attestation-report");
      signed_report sr;
      if (!sr.ParseFromString(t_str)) {
        printf("ParseFromString failed (1)\n");
        return false;
      }
      if (!verify_report(type, t_str, sr.signing_key())) {
        printf("verify_report failed\n");
        return false;
      }
      vse_attestation_report_info info;
      if (!info.ParseFromString(sr.report())) {
        printf("ParseFromString failed (2)\n");
        return false;
      }

      if (!check_date_range(info.not_before(), info.not_after())) {
        printf("check_date_range failed\n");
        return false;
      }

      attestation_user_data ud;
      if (!ud.ParseFromString(info.user_data())) {
        printf("ParseFromString failed (3)\n");
        return false;
      }
      key_message attest_key;
      vse_clause* cl_to_insert = already_proved->add_proved();
      if (!construct_vse_attestation_statement(sr.signing_key(),
            ud.enclave_key(), info.verified_measurement(), cl_to_insert)) {
        printf("construct_vse_attestation_statement failed\n");
        return false;
      }
    } else {
      printf("Unknown evidence type: %i\n", i);
      print_evidence(evp.fact_assertion(i)); printf("\n");
      printf("Unknown evidence type: %s\n", evp.fact_assertion(i).evidence_type().c_str());
      return false;
    }
  }
  return true;
}

// R1: If measurement is-trusted and key1 speaks-for measurement then
//    key1 is-trusted-for-authentication.
bool verify_rule_1(predicate_dominance& dom_tree, const vse_clause& c1,
        const vse_clause& c2, const vse_clause& conclusion) {

  // Make sure clauses are in the right form.
  if (!c1.has_subject() || !c1.has_verb())
    return false;
  if (c1.has_object() || c1.has_clause())
    return false;
  if (c1.verb() != "is-trusted")
    return false;
  if (c1.subject().entity_type() != "measurement")
    return false;

  if (!c2.has_subject() || !c2.has_verb())
    return false;
  if (c2.verb() != "speaks-for")
    return false;
  if (!c2.has_object() || c2.has_clause())
    return false;
  if (c2.object().entity_type() != "measurement")
    return false;

  if (!same_entity(c1.subject(), c2.object()))
    return false;
  // Make sure subject of conclusion is subject of c2 and verb "is-trusted"
  if (!conclusion.has_subject() || !conclusion.has_verb() || 
       conclusion.has_object() || conclusion.has_clause())
    return false;
  if (conclusion.verb() != "is-trusted" &&
      conclusion.verb() != "is-trusted-for-authentication")
    return false;

  return same_entity(conclusion.subject(), c2.subject());
}

// R2: If key2 speaks-for key1 and key3 speaks-for key2 then key3 speaks-for key1
bool verify_rule_2(predicate_dominance& dom_tree, const vse_clause& c1,
        const vse_clause& c2, const vse_clause& conclusion) {
  return false;
}

// R3: If key1 is-trusted and key1 says X, then X is true
bool verify_rule_3(predicate_dominance& dom_tree, const vse_clause& c1, const vse_clause& c2, const vse_clause& conclusion) {
  if (!c1.has_subject() || !c1.has_verb())
    return false;
  if (c1.has_object() || c1.has_clause())
    return false;
  if (c1.verb() != "is-trusted")
    return false;

  if (!c2.has_subject() || !c2.has_verb())
    return false;
  if (c2.has_object() || !c2.has_clause())
    return false;
  if (c2.verb() != "says")
    return false;
  if (!same_entity(c1.subject(), c2.subject()))
    return false;
  return same_vse_claim(c2.clause(), conclusion);
}

// R4: If key2 speaks-for key1 and key1 is-trustedXXX then key2 is-trustedXXX
bool verify_rule_4(predicate_dominance& dom_tree, const vse_clause& c1,
    const vse_clause& c2, const vse_clause& conclusion) {
  return false;
}

// R5: If key1 is-trustedXXX and key1 says key2 is-trustedYYY then then key2 is-trustedYYY
//    provided is-trustedXXX dominates is-trustedYYY
bool verify_rule_5(predicate_dominance& dom_tree, const vse_clause& c1,
      const vse_clause& c2, const vse_clause& conclusion) {

  if (!c1.has_subject() || !c1.has_verb())
    return false;
  if (c1.has_object() || c1.has_clause())
    return false;
  if (!c2.has_subject() || !c2.has_verb())
    return false;
  if (c2.verb() != "says")
    return false;
  if (c2.has_object() || !c2.has_clause())
    return false;

  if (!same_entity(c1.subject(), c2.subject()))
    return false;

  if (!c2.clause().has_subject() || !c2.clause().has_verb())
    return false;
  if (c2.clause().has_object() || c2.clause().has_clause())
    return false;

  if (!dominates(dom_tree, c1.verb(), c2.clause().verb()))
    return false;
  return same_vse_claim(c2.clause(), conclusion);
}

// R6: if key1 is-trustedXXX and key1 says key2 speaks-for measurement then
//      key2 speaks-for measurement provided is-trustedXXX dominates is-trusted-for-attestation
bool verify_rule_6(predicate_dominance& dom_tree, const vse_clause& c1,
      const vse_clause& c2, const vse_clause& conclusion) {

  if (!c1.has_subject() || !c1.has_verb())
    return false;
  if (c1.has_object() || c1.has_clause())
    return false;
  string p1 = c1.verb();

  if (!c2.has_subject() || !c2.has_verb())
    return false;
  if (c2.has_object() || !c2.has_clause())
    return false;
  if (c2.verb() != "says")
    return false;

  if (!c2.clause().has_subject() || !c2.clause().has_verb())
    return false;
  if (!c2.clause().has_object() || c2.clause().has_clause())
    return false;
  if (c2.clause().verb() != "speaks-for")
    return false;

  if (c2.clause().subject().entity_type() != "key")
    return false;
  if (c2.clause().object().entity_type() != "measurement")
    return false;

  if (!same_entity(c1.subject(), c2.subject()))
    return false;

  string p2("is-trusted-for-attestation");
  if (!dominates(dom_tree, c1.verb(), p2))
    return false;

  return same_vse_claim(c2.clause(), conclusion);
}

// R7: if measurement is-trusted
//  key2 speaks-for measurement then
//  key2 is-trusted-for-attestation
//      provided is-trustedXXX dominates is-trusted-for-attestation
bool verify_rule_7(predicate_dominance& dom_tree, const vse_clause& c1,
      const vse_clause& c2, const vse_clause& conclusion) {

  // Make sure clauses are in the right form.
  if (!c1.has_subject() || !c1.has_verb())
    return false;
  if (c1.has_object() || c1.has_clause())
    return false;
  if (c1.verb() != "is-trusted")
    return false;
  if (c1.subject().entity_type() != "measurement")
    return false;

  if (!c2.has_subject() || !c2.has_verb())
    return false;
  if (c2.verb() != "speaks-for")
    return false;
  if (!c2.has_object() || c2.has_clause())
    return false;
  if (c2.object().entity_type() != "measurement")
    return false;

  if (!same_entity(c1.subject(), c2.object()))
    return false;
  // Make sure subject of conclusion is subject of c2 and verb "is-trusted"
  if (!conclusion.has_subject() || !conclusion.has_verb() || 
       conclusion.has_object() || conclusion.has_clause())
    return false;
  if (conclusion.verb() != "is-trusted-for-attestation")
    return false;
  return same_entity(conclusion.subject(), c2.subject());
}

bool verify_external_proof_step(predicate_dominance& dom_tree, proof_step& step) {
  if (!step.has_rule_applied())
    return false;
  if (!step.has_s1() || !step.has_s2()|| !step.has_conclusion())
    return false;
  switch(step.rule_applied()) {
    default:
      return false;
  case 1:
    return verify_rule_1(dom_tree, step.s1(), step.s2(), step.conclusion());
  case 2:
    return verify_rule_2(dom_tree, step.s1(), step.s2(), step.conclusion());
  case 3:
    return verify_rule_3(dom_tree, step.s1(), step.s2(), step.conclusion());
  case 4:
    return verify_rule_4(dom_tree, step.s1(), step.s2(), step.conclusion());
  case 5:
    return verify_rule_5(dom_tree, step.s1(), step.s2(), step.conclusion());
  case 6:
    return verify_rule_6(dom_tree, step.s1(), step.s2(), step.conclusion());
  case 7:
    return verify_rule_7(dom_tree, step.s1(), step.s2(), step.conclusion());
  }
  return false;
}

bool verify_internal_proof_step(predicate_dominance& dom_tree,
      vse_clause s1, vse_clause s2, vse_clause conclude, int rule_to_apply) {
  if (rule_to_apply < 1 || rule_to_apply > 6)
    return false;
  switch(rule_to_apply) {
    default:
      return false;
    case 1:
      return verify_rule_1(dom_tree, s1, s2, conclude);
    case 2:
      return verify_rule_2(dom_tree, s1, s2, conclude);
    case 3:
      return verify_rule_3(dom_tree, s1, s2, conclude);
    case 4:
      return verify_rule_4(dom_tree, s1, s2, conclude);
    case 5:
      return verify_rule_5(dom_tree, s1, s2, conclude);
    case 6:
      return verify_rule_6(dom_tree, s1, s2, conclude);
    case 7:
      return verify_rule_7(dom_tree, s1, s2, conclude);
  }
  return true;
}

bool verify_proof(key_message& policy_pk, vse_clause& to_prove,
        predicate_dominance& dom_tree,
        proof *the_proof, proved_statements* are_proved) {

  // verify proof
  for (int i = 0; i < the_proof->steps_size(); i++) {
    bool success;
    if (!statement_already_proved(the_proof->steps(i).s1(), are_proved))

    if (!statement_already_proved(the_proof->steps(i).s2(), are_proved))
      return false;
    success = verify_internal_proof_step(dom_tree,
              the_proof->steps(i).s1(), the_proof->steps(i).s2(),
              the_proof->steps(i).conclusion(), the_proof->steps(i).rule_applied());
    if (!success) {
      printf("Proof step %d failed, rule: %d\n", i, the_proof->steps(i).rule_applied());
      print_vse_clause(the_proof->steps(i).conclusion()); printf("\n");
      return false;
    }
    vse_clause* to_add = are_proved->add_proved();
    to_add->CopyFrom(the_proof->steps(i).conclusion());
  }

  int n = are_proved->proved_size();
  if (n < 1)
    return false;
  const vse_clause& last_proved = are_proved->proved(n-1);
  return same_vse_claim(to_prove, last_proved);
}

// old style
// ---------------------------------------------------------------------------------------

bool add_newfacts_for_sev_attestation(key_message& policy_pk, string& serialized_ark_cert,
      string& serialized_ask_cert, string& serialized_vcek_cert,
      signed_claim_sequence& trusted_platforms, signed_claim_sequence& trusted_measurements,
      proved_statements* already_proved) {

  // At this point, the already_proved should be
  //    "policyKey is-trusted"
  //    "The ARK-key says the ARK-key is-trusted-for-attestation"
  //    "The ARK-key says the ASK-key is-trusted-for-attestation"
  //    "The ASK-key says the VCEK-key is-trusted-for-attestation"
  //    "VCEK says the enclave-key speaks-for the measurement
  // Add
  //    "The policy-key says the ARK-key is-trusted-for-attestation
  //    "The policy-key says the measurement is-trusted

  signed_claim_message sc1;
  if (!already_proved->proved(1).has_subject()) {
    printf("add_newfacts_for_sev_attestation: error 1\n");
    return false;
  }
  if (already_proved->proved(1).subject().entity_type() != "key") {
    printf("add_newfacts_for_sev_attestation: error 2\n");
    return false;
  }
  const key_message& expected_key = already_proved->proved(1).subject().key();
  if (!get_signed_platform_claim_from_trusted_list(expected_key,
        trusted_platforms, &sc1)) {
    printf("add_newfacts_for_sev_attestation: error 3\n");
    return false;
  }
  if (!add_fact_from_signed_claim(sc1, already_proved)) {
    printf("add_newfacts_for_sev_attestation: error 4\n");
    return false;
  }

  if (!already_proved->proved(4).has_clause()) {
    printf("add_newfacts_for_sev_attestation: error 5\n");
    return false;
  }
  if (!already_proved->proved(4).clause().has_object()) {
    printf("add_newfacts_for_sev_attestation: error 6\n");
    return false;
  }
  const entity_message& m_ent = already_proved->proved(4).clause().object();
  string expected_measurement;
  expected_measurement.assign((char*)m_ent.measurement().data(), m_ent.measurement().size());

  signed_claim_message sc2;
  if (!get_signed_measurement_claim_from_trusted_list(expected_measurement,
        trusted_measurements, &sc2)) {
    printf("add_newfacts_for_sev_attestation: error 7\n");
    return false;
  }
  if (!add_fact_from_signed_claim(sc2, already_proved)) {
    printf("add_newfacts_for_sev_attestation: error 8\n");
    return false;
  }

  return true;
}

bool add_newfacts_for_sdk_platform_attestation(key_message& policy_pk,
      signed_claim_sequence& trusted_platforms, signed_claim_sequence& trusted_measurements,
      proved_statements* already_proved) {
  // At this point, the already_proved should be
  //      "policyKey is-trusted"
  //      "platformKey says attestationKey is-trusted
  //      "enclaveKey speaks-for measurement"
  // Add
  //   "policyKey says measurement is-trusted"
  if (!already_proved->proved(2).has_object()) {
    printf("Add_newfacts_for_oeplatform_attestation: no speaksfor\n");
    return false;
  }

  // "enclaveKey speaks-for measurement"
  string expected_measurement;
  if (!already_proved->proved(2).has_object())
    return false;
  const entity_message& m_ent = already_proved->proved(2).object();

  expected_measurement.assign((char*)m_ent.measurement().data(), m_ent.measurement().size());

  signed_claim_message sc;
  if (!get_signed_measurement_claim_from_trusted_list(expected_measurement,
        trusted_measurements, &sc))
    return false;
  if (!add_fact_from_signed_claim(sc, already_proved))
    return false;

  return true;
}

bool add_new_facts_for_abbreviatedplatformattestation(key_message& policy_pk,
      signed_claim_sequence& trusted_platforms, signed_claim_sequence& trusted_measurements,
      proved_statements* already_proved) {

  // At this point, the already_proved should be
  //    "policyKey is-trusted"
  //    "platformKey says attestationKey is-trusted
  //    "attestKey says enclaveKey speaks-for measurement
  // Add
  //    "policyKey says measurement is-trusted"
  //    "policyKey says platformKey is-trusted-for-attestation"

  // "attestKey says enclaveKey speaks-for measurement
  string expected_measurement;
  if (!already_proved->proved(2).has_clause()) {
    return false;
  }
  if (!already_proved->proved(2).clause().has_object()) {
    return false;
  }
  const entity_message& m_ent = already_proved->proved(2).clause().object();
  expected_measurement.assign((char*)m_ent.measurement().data(), m_ent.measurement().size());
  signed_claim_message sc;
  if (!get_signed_measurement_claim_from_trusted_list(expected_measurement,
        trusted_measurements, &sc)) {
    return false;
  }
  if (!add_fact_from_signed_claim(sc, already_proved)) {
    return false;
  }

  // "platformKey says attestationKey is-trusted
  if (!already_proved->proved(1).has_subject()) {
    return false;
  }
  if (already_proved->proved(1).subject().entity_type() != "key") {
    return false;
  }
  const key_message& expected_key = already_proved->proved(1).subject().key();
  if (!get_signed_platform_claim_from_trusted_list(expected_key,
        trusted_platforms, &sc)) {
    return false;
  }
  if (!add_fact_from_signed_claim(sc, already_proved)) {
    return false;
  }

  return true;
}

bool construct_proof_from_sev_evidence(key_message& policy_pk, const string& purpose,
      proved_statements* already_proved, vse_clause* to_prove, proof* pf) {

  // At this point, the already_proved should be
  //    0: "policyKey is-trusted"
  //    1: "The ARK-key says the ARK-key is-trusted-for-attestation"
  //    2: "The ARK-key says the ASK-key is-trusted-for-attestation"
  //    3: "The ASK-key says the VCEK-key is-trusted-for-attestation"
  //    4: "VCEK says the enclave-key speaks-for the measurement
  //    5: "The policy-key says the ARK-key is-trusted-for-attestation
  //    6: "policyKey says measurement is-trusted"

  // Proof is:
  //    "policyKey is-trusted" AND policyKey says measurement is-trusted" -->
  //        "the measurement is-trusted" (R3)
  //    "policyKey is-trusted" AND
  //        "policy-key says the ARK-key is-trusted-for-attestation" -->
  //        "the ARK-key is-trusted-for-attestation" (R3)
  //    "the ARK-key is-trusted-for-attestation" AND
  //        "The ARK-key says the ASK-key is-trusted-for-attestation" -->
  //        "the ASK-key is-trusted-for-attestation" (R5)
  //    "the ASK-key is-trusted-for-attestation" AND
  //        "the ASK-key says the VCEK-key is-trusted-for-attestation" -->
  //        "the VCEK-key is-trusted-for-attestation" (R5)
  //    "the VCEK-key is-trusted-for-attestation" AND
  //        "the VCEK-key says the enclave-key speaks-for the measurement" -->
  //        "enclave-key speaks-for the measurement"  (R6)
  //    "enclave-key speaks-for the measurement" AND "the measurement is-trusted" -->
  //        "the enclave key is-trusted-for-authentication" (R1) OR
  //        "the enclave key is-trusted-for-attestation" (R6)

  //  Final fact list:
  //       0: "policyKey is-trusted"
  //       1: "The ARK-key says the ARK-key is-trusted-for-attestation"
  //       2: "The ARK-key says the ASK-key is-trusted-for-attestation"
  //       3: "The ASK-key says the VCEK-key is-trusted-for-attestation"
  //       4: "The policy-key says the ARK-key is-trusted-for-attestation
  //       5: "VCEK says the enclave-key speaks-for the measurement
  //       6: "The policy-key says the ARK-key is-trusted-for-attestation
  //       7: "policyKey says measurement is-trusted"
  //       8: "ark-key is-trusted-for-attestation"
  //       9: "measurement is-trusted"
  //      10: "ask-key is-trusted-for-attestation"
  //      11: "vcek-key is-trusted-for-attestation"
  //      12: "enclave_key speaks-for measurement"
  //      13: "enclave-key is-trusted-for-authentication


#ifdef PRINT_ALREADY_PROVED
  printf("construct proof from sev evidence, initial proved statements:\n");
  for (int i = 0; i < already_proved->proved_size(); i++) {
    print_vse_clause(already_proved->proved(i));
    printf("\n");
  }
  printf("\n");
#endif

  if (already_proved->proved_size() != 7) {
    printf("construct_proof_from_sev_evidence: bad size\n");
    return false;
  }
  if (!already_proved->proved(2).has_clause() || !already_proved->proved(2).clause().has_subject()) {
    printf("construct_proof_from_sev_evidence: ill formed statement 2\n");
    return false;
  }
  const entity_message& enclave_key = already_proved->proved(4).clause().subject();
  string it("is-trusted-for-authentication");
  string it2("is-trusted-for-attestation");
  if (purpose == "attestation") {
    if (!make_unary_vse_clause(enclave_key, it2, to_prove))
        return false;
  } else {
    if (!make_unary_vse_clause(enclave_key, it, to_prove))
        return false;
  }

  proof_step* ps = nullptr;

  // "policyKey is-trusted" AND "policyKey says ark-key is-trusted-for-attestation"
  //     --> "ark-key is-trusted-for-attestation"
  if (!already_proved->proved(4).has_clause()) {
    printf("construct_proof_from_sev_evidence: Error 2\n");
    return false;
  }
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(already_proved->proved(0));
  ps->mutable_s2()->CopyFrom(already_proved->proved(5));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(5).clause());
  ps->set_rule_applied(3);
  const vse_clause& ark_key_is_trusted = ps->conclusion();

  // "policyKey is-trusted" AND "policyKey says measurement is-trusted" --> "measurement is-trusted"
  if (!already_proved->proved(3).has_clause()) {
    printf("construct_proof_from_sev_evidence: Error 3\n");
    return false;
  }
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(already_proved->proved(0));
  ps->mutable_s2()->CopyFrom(already_proved->proved(6));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(6).clause());
  ps->set_rule_applied(3);
  const vse_clause& measurement_is_trusted = ps->conclusion();

  // "ark-key is-trusted-for-attestation" AND "ark-key says ask-key is-trusted-for-attestation"
  //      --> "ask-key is-trusted-for-attestation"
  if (!already_proved->proved(3).has_clause()) {
    printf("construct_proof_from_sev_evidence: Error 4\n");
    return false;
  }
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(ark_key_is_trusted);
  ps->mutable_s2()->CopyFrom(already_proved->proved(2));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(2).clause());
  ps->set_rule_applied(5);
  const vse_clause& ask_key_is_trusted = ps->conclusion();

  // "ask-key is-trusted-for-attestation" AND "ask-key says vcek-key is-trusted-for-attestation"
  //      --> "vcek-key is-trusted-for-attestation"
  if (!already_proved->proved(3).has_clause()) {
    printf("construct_proof_from_sev_evidence: Error 4\n");
    return false;
  }
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(ask_key_is_trusted);
  ps->mutable_s2()->CopyFrom(already_proved->proved(3));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(3).clause());
  ps->set_rule_applied(5);
  const vse_clause& vcek_key_is_trusted = ps->conclusion();

  // "vcek-Key is-trusted-for-attestation" AND  "vcek-Key says enclaveKey speaks-for measurement"
  //      --> "enclaveKey speaks-for measurement"
  if (!already_proved->proved(5).has_clause()) {
    printf("construct_proof_from_sev_evidence: Error 5\n");
    return false;
  }
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(vcek_key_is_trusted);
  ps->mutable_s2()->CopyFrom(already_proved->proved(4));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(4).clause());
  ps->set_rule_applied(6);
  const vse_clause& enclave_speaksfor_measurement = ps->conclusion();

  // "measurement is-trusted" AND "enclaveKey speaks-for measurement"
  //      --> "enclaveKey is-trusted-for-authentication"
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(measurement_is_trusted);
  ps->mutable_s2()->CopyFrom(enclave_speaksfor_measurement);
  ps->mutable_conclusion()->CopyFrom(*to_prove);
  if (purpose == "attestation") {
    ps->set_rule_applied(7);
  } else {
    ps->set_rule_applied(1);
  }

  return true;
}

bool construct_proof_from_sdk_evidence(key_message& policy_pk, const string& purpose,
      proved_statements* already_proved,
      vse_clause* to_prove, proof* pf) {

  // At this point, the already_proved should be
  //    "policyKey is-trusted"
  //    "platformKey says attestationKey is-trusted
  //    "enclaveKey speaks-for measurement"
  //    "policyKey says measurement is-trusted"

  if (!already_proved->proved(2).has_subject()) {
    printf("Error 1, construct_proof_from_sdk_evidence\n");
    return false;
  }
  string it("is-trusted");
  if (!make_unary_vse_clause(already_proved->proved(2).subject(), it, to_prove)) {
      printf("Error 2, construct_proof_from_sdk_evidence\n");
      return false;
  }

  proof_step* ps = nullptr;

  //  "policyKey is-trusted" AND "policyKey says measurement is-trusted" --> "measurement is-trusted"
  const entity_message& m_ent = already_proved->proved(2).object();
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(already_proved->proved(0));
  ps->mutable_s2()->CopyFrom(already_proved->proved(3));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(3).clause());
  ps->set_rule_applied(3);
  const vse_clause& platformkey_is_trusted = ps->conclusion();

  //  "measurement is-trusted" AND "enclaveKey speaks-for measurement --> "enclaveKey is trusted"
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(already_proved->proved(3).clause());
  ps->mutable_s2()->CopyFrom(already_proved->proved(2));
  ps->mutable_conclusion()->CopyFrom(*to_prove);
  ps->set_rule_applied(1);

  return true;
}

bool construct_proof_from_full_vse_evidence(key_message& policy_pk, const string& purpose,
      proved_statements* already_proved, vse_clause* to_prove, proof* pf) {

  // At this point, the already_proved should be
  //      "policyKey is-trusted"
  //      "platformKey says attestKey is-trusted-for-attestation
  //      "attestKey says enclaveKey speaks-for measurement
  //      "policyKey says measurement is-trusted"
  //      "policyKey says platformKey is-trusted-for-attestation"

  if (already_proved->proved_size() != 5) {
    printf("Error 0, construct_proof_from_full_vse_evidence\n");
    return false;
  }

  if (!already_proved->proved(2).has_clause() || !already_proved->proved(2).clause().has_subject()) {
    printf("Error 1, construct_proof_from_full_vse_evidence\n");
    return false;
  }
  const entity_message& enclave_key = already_proved->proved(2).clause().subject();
  string it("is-trusted-for-authentication");
  if (!make_unary_vse_clause(enclave_key, it, to_prove))
      return false;

  proof_step* ps = nullptr;

  // "policyKey is-trusted" AND "policyKey says platformKey is-trusted-for-attestation"
  //     --> "platformKey is-trusted-for-attestation"
  if (!already_proved->proved(4).has_clause()) {
    printf("Error 2, construct_proof_from_full_vse_evidence\n");
    return false;
  }
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(already_proved->proved(0));
  ps->mutable_s2()->CopyFrom(already_proved->proved(4));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(4).clause());
  ps->set_rule_applied(3);
  const vse_clause& platformkey_is_trusted = ps->conclusion();

  // "policyKey is-trusted" AND "policyKey says measurement is-trusted" --> "measurement is-trusted"
  if (!already_proved->proved(3).has_clause()) {
    printf("Error 3, construct_proof_from_full_vse_evidence\n");
    return false;
  }
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(already_proved->proved(0));
  ps->mutable_s2()->CopyFrom(already_proved->proved(3));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(3).clause());
  ps->set_rule_applied(3);
  const vse_clause& measurement_is_trusted = ps->conclusion();

  // "platformKey is-trusted-for-attestation" AND "platformKey says attestKey is-trusted-for-attestation"
  //      --> "attestKey is-trusted"
  if (!already_proved->proved(1).has_clause()) {
    printf("Error 4, construct_proof_from_full_vse_evidence\n");
    return false;
  }
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(platformkey_is_trusted);
  ps->mutable_s2()->CopyFrom(already_proved->proved(1));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(1).clause());
  ps->set_rule_applied(5);
  const vse_clause& attestkey_is_trusted = ps->conclusion();

  // "attestKey is-trusted-for-attestation" AND  "attestKey says enclaveKey speaks-for measurement"
  //      --> "enclaveKey speaks-for measurement"
  if (!already_proved->proved(2).has_clause()) {
    printf("Error 5, construct_proof_from_full_vse_evidence\n");
    return false;
  }
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(attestkey_is_trusted);
  ps->mutable_s2()->CopyFrom(already_proved->proved(2));
  ps->mutable_conclusion()->CopyFrom(already_proved->proved(2).clause());
  ps->set_rule_applied(6);
  const vse_clause& enclave_speaksfor_measurement = ps->conclusion();

  // "measurement is-trusted" AND "enclaveKey speaks-for measurement"
  //      --> "enclaveKey is-trusted-for-authentication"
  ps = pf->add_steps();
  ps->mutable_s1()->CopyFrom(measurement_is_trusted);
  ps->mutable_s2()->CopyFrom(enclave_speaksfor_measurement);
  ps->mutable_conclusion()->CopyFrom(*to_prove);
  ps->set_rule_applied(1);

  return true;
}

bool construct_proof_from_request(const string& evidence_descriptor, key_message& policy_pk,
      const string& purpose, signed_claim_sequence& trusted_platforms,
      signed_claim_sequence& trusted_measurements, evidence_package& evp,
      proved_statements* already_proved, vse_clause* to_prove, proof* pf) {

  if (!init_proved_statements(policy_pk, evp, already_proved)) {
    printf("init_proved_statements returned false\n");
    return false;
  }

#ifdef PRINT_ALREADY_PROVED
  printf("construct proof from request, initial proved statements:\n");
  for (int i = 0; i < already_proved->proved_size(); i++) {
    print_vse_clause(already_proved->proved(i));
    printf("\n");
  }
  printf("\n");
#endif

  if (evidence_descriptor == "full-vse-support") {
    if (!construct_proof_from_full_vse_evidence(policy_pk, purpose, already_proved, to_prove, pf))
      return false;
  } else if (evidence_descriptor == "platform-attestation-only") {
    if (!add_new_facts_for_abbreviatedplatformattestation(policy_pk, 
              trusted_platforms, trusted_measurements, already_proved)) {
      printf("add_new_facts_for_abbreviatedplatformattestation failed\n");
      return false;
    }
    if (!construct_proof_from_full_vse_evidence(policy_pk, purpose, already_proved, to_prove, pf)) {
      printf("construct_proof_from_full_vse_evidence in construct_proof_from_request failed\n");
      return false;
    }
  } else if (evidence_descriptor == "sev-evidence") {
    string serialized_ark_cert;
    string serialized_ask_cert;
    string serialized_vcek_cert;
    if (!add_newfacts_for_sev_attestation(policy_pk, serialized_ark_cert,
            serialized_ask_cert, serialized_vcek_cert,
            trusted_platforms, trusted_measurements, already_proved)) {
      printf("construct_proof_from_sev_evidence failed in add_newfacts_for_sev_attestation\n");
      return false;
    }
    if (!construct_proof_from_sev_evidence(policy_pk, purpose, already_proved, to_prove, pf))
      return false;
  } else if (evidence_descriptor == "oe-evidence") {
    if (!add_newfacts_for_sdk_platform_attestation(policy_pk,
              trusted_platforms, trusted_measurements, already_proved))
      return false;
    return construct_proof_from_sdk_evidence(policy_pk, purpose, already_proved, to_prove, pf);
  } else if (evidence_descriptor == "asylo-evidence") {
    if (!add_newfacts_for_sdk_platform_attestation(policy_pk,
              trusted_platforms, trusted_measurements, already_proved)) {
      printf("construct_proof_from_full_vse_evidence in add_newfacts_for_asyloplatform_evidence failed\n");
      return false;
    }
    return construct_proof_from_sdk_evidence(policy_pk, purpose, already_proved, to_prove, pf);
  } else if (evidence_descriptor == "gramine-evidence") {
    if (!add_newfacts_for_sdk_platform_attestation(policy_pk,
              trusted_platforms, trusted_measurements, already_proved)) {
      printf("construct_proof_from_full_vse_evidence in add_newfacts_for_gramineplatform_evidence failed\n");
      return false;
    }
    return construct_proof_from_sdk_evidence(policy_pk, purpose, already_proved, to_prove, pf);
  } else {
    return false;
  }

  return true;
}

bool validate_evidence(const string& evidence_descriptor, signed_claim_sequence& trusted_platforms,
        signed_claim_sequence& trusted_measurements, const string& purpose,
        evidence_package& evp, key_message& policy_pk) {

  proved_statements already_proved;
  vse_clause to_prove;
  proof pf;
  predicate_dominance predicate_dominance_root;

  if (!init_dominance_tree(predicate_dominance_root)) {
    printf("validate_evidence: can't init predicate dominance tree\n");
    return false;
  }

  if (!init_axiom(policy_pk, &already_proved)) {
    printf("validate_evidence: can't init axiom\n");
    return false;
  }

  if (!construct_proof_from_request(evidence_descriptor, policy_pk, purpose,
            trusted_platforms, trusted_measurements,
            evp, &already_proved, &to_prove, &pf)) {
    printf("validate_evidence: can't construct proof\n");
    return false;
  }

#ifdef PRINT_ALREADY_PROVED
  printf("proved statements after additions:\n");
  for (int i = 0; i < pf.steps_size(); i++) {
    print_vse_clause(already_proved.proved(i));
    printf("\n");
  }
  printf("\n");

  printf("to prove : ");
  print_vse_clause(to_prove);
  printf("\n\n");
  printf("proposed proof:\n");
  print_proof(pf);
  printf("\n");
#endif

  if (!verify_proof(policy_pk, to_prove, predicate_dominance_root,
            &pf, &already_proved)) {
    printf("verify_proof failed\n");
    return false;
  }

#ifdef PRINT_ALREADY_PROVED
  printf("Proved:"); print_vse_clause(to_prove); printf("\n");
  printf("final proved statements:\n");
  for (int i = 0; i < already_proved.proved_size(); i++) {
    print_vse_clause(already_proved.proved(i));
    printf("\n");
  }
  printf("\n");
#endif

  return true;
}

//  New style proofs with platform information
// -------------------------------------------------------------------

// Policy
//   1. "policyKey is-trusted"
//   2: "The policyKey says the ARK-key is-trusted-for-attestation"
//   3: "policyKey says measurement is-trusted"
//   4. "policyKey says platform[amd-sev-snp, no-debug, no-migrate, api-major >= 0, api-minor >= 0]
//          has-trusted-platform-property"

// From sev keys
//    5: "The ARK-key says the ARK-key is-trusted-for-attestation"
//    6: "The ARK-key says the ASK-key is-trusted-for-attestation"
//    7: "The ASK-key says the VCEK-key is-trusted-for-attestation"

// From Attestation
//    8: "VCEK says environment(platform, measurement) is-environment"
//    9: "VCEK says enclave-key speaks-for environment"

// When we start constructing the proof, already_proved should be
//    0: "policyKey is-trusted"
//    1: "The policy-key says the ARK-key is-trusted-for-attestation"
//    2: "policyKey says measurement is-trusted"
//    3. "policyKey says platform[amd-sev-snp, no-debug, no-migrate, api-major >= 0, api-minor >= 0]
//            has-trusted-platform-property"
//    4: "VCEK says environment(platform, measurement) is-environment"
//    5: "VCEK says enclave-key speaks-for environment"
//    6: "The ARK-key says the ARK-key is-trusted-for-attestation"
//    7: "The ARK-key says the ASK-key is-trusted-for-attestation"
//    8: "The ASK-key says the VCEK-key is-trusted-for-attestation"

// Proof is:
//    "policyKey is-trusted" AND policyKey says measurement is-trusted" -->
//        "measurement is-trusted" (R3)
//    "policyKey is-trusted" AND
//        "policy-key says the ARK-key is-trusted-for-attestation" -->
//        "the ARK-key is-trusted-for-attestation" (R3)
//    "the ARK-key is-trusted-for-attestation" AND
//        "The ARK-key says the ASK-key is-trusted-for-attestation" -->
//        "the ASK-key is-trusted-for-attestation" (R5)
//    "the ASK-key is-trusted-for-attestation" AND
//        "the ASK-key says the VCEK-key is-trusted-for-attestation" -->
//        "the VCEK-key is-trusted-for-attestation" (R5)
//    "VCEK-key is-trusted-for-attestation" AND
//        "the VCEK says environment(platform, measurement) is-environment -->
//        "environment(platform, measurement) is-environment"
//    "environment(platform, measurement) is-environment" AND
//        "platform[amd-sev-snp, no-debug,...] has-trusted-platform-property" -->
//        "environment(platform, measurement) environment-platform-is-trusted"
//    "environment(platform, measurement) is-environment" AND
//        "measurement is-trusted" -->
//        "environment(platform, measurement) environment-measurement-is-trusted"
//    "environment(platform, measurement) environment-platform-is-trusted" AND
//        "environment(platform, measurement) environment-measurement-is-trusted"  -->
//        "environment(platform, measurement) is-trusted
//    "VCEK-key is-trusted-for-attestation" AND
//      "VCEK-key says the enclave-key speaks-for the environment()" -->
//        "enclave-key speaks-for the environment()"
//    "environment(platform, measurement) is-trusted AND
//        enclave-key speaks-for environment(platform, measurement)  -->
//        enclave-key is-trusted-for-authentication  [or enclave-key is-trusted-for-attestation]

bool get_properties_from_sev_attest(byte* attestation, properties* props) {
  // at->policy is policy
  // at->measurement is measurement
  // at->report_data is hash of user_data
  // at->platform_version is platform data
  return false;
}

bool construct_proof_from_sev_evidence_with_plat(const string& evidence_descriptor,
      key_message& policy_pk, const string& purpose,
      proved_statements* already_proved, vse_clause* to_prove, proof* pf) {
  return true;
}

bool init_policy(signed_claim_sequence& policy, key_message& policy_pk,
      proved_statements* already_proved) {

  for (int i = 0; i < policy.claims_size(); i++) {
#if 0
    const entity_message& em = policy.claims(i).subject();
    if (!em.entity_type() == "key" || !same_key(policy_pk, em.key())) {
      printf("init_policy: Policy must have\n");
    }
#endif
    if (!add_fact_from_signed_claim(policy.claims(i), already_proved)) {
      printf("init_policy: Can't add claim %d\n", i);
      // print_signed_claim(policy.claims(i));
      printf("\n");
      return false;
    }
  }

  return true;
}

// Use policy statements for init
bool validate_evidence_from_policy(const string& evidence_descriptor,
        signed_claim_sequence& policy, const string& purpose,
        evidence_package& evp, key_message& policy_pk) {

  proved_statements already_proved;
  vse_clause to_prove;
  proof pf;
  predicate_dominance predicate_dominance_root;

  if (!init_dominance_tree(predicate_dominance_root)) {
    printf("validate_evidence: can't init predicate dominance tree\n");
    return false;
  }

  if (!init_axiom(policy_pk, &already_proved)) {
    printf("validate_evidence: can't init axiom\n");
    return false;
  }

  if (!init_policy(policy, policy_pk, &already_proved)) {
    printf("validate_evidence: init_policy failed\n");
    return false;
  }

  if (!init_proved_statements(policy_pk, evp, &already_proved)) {
    printf("validate_evidence: init_proved_statements\n");
    return false;
  }

#if 1
  printf("After init_policy --- proved statements:\n");
  for (int i = 0; i < already_proved.proved_size(); i++) {
    printf("  %2d: ", i);
    print_vse_clause(already_proved.proved(i));
    printf("\n");
    printf("\n");
  }
  printf("\n");
#endif

return true;

  if (!construct_proof_from_sev_evidence_with_plat(evidence_descriptor,
          policy_pk, purpose, &already_proved, &to_prove, &pf)) {
    printf("validate_evidence: can't construct proof\n");
    return false;
  }

#ifdef PRINT_ALREADY_PROVED
  printf("proved statements after additions:\n");
  for (int i = 0; i < pf.steps_size(); i++) {
    print_vse_clause(already_proved.proved(i));
    printf("\n");
  }
  printf("\n");

  printf("to prove : ");
  print_vse_clause(to_prove);
  printf("\n\n");
  printf("proposed proof:\n");
  print_proof(pf);
  printf("\n");
#endif

  if (!verify_proof(policy_pk, to_prove, predicate_dominance_root,
            &pf, &already_proved)) {
    printf("verify_proof failed\n");
    return false;
  }
#ifdef PRINT_ALREADY_PROVED
  printf("Proved:"); print_vse_clause(to_prove); printf("\n");
  printf("final proved statements:\n");
  for (int i = 0; i < already_proved.proved_size(); i++) {
    print_vse_clause(already_proved.proved(i));
    printf("\n");
  }
  printf("\n");
#endif

  return true;
}

// -------------------------------------------------------------------------------------
