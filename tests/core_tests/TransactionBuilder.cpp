// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2014-2015 XDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "TransactionBuilder.h"

using namespace cryptonote;


TransactionBuilder::TransactionBuilder(const cryptonote::Currency& currency, uint64_t unlockTime)
  : m_currency(currency), m_version(cryptonote::TRANSACTION_VERSION_1), m_unlockTime(unlockTime), m_txKey(KeyPair::generate()) {}

TransactionBuilder& TransactionBuilder::newTxKeys() {
  m_txKey = KeyPair::generate();
  return *this;
}

cryptonote::KeyPair TransactionBuilder::getTxKeys() const {
  return m_txKey;
}

TransactionBuilder& TransactionBuilder::setTxKeys(const cryptonote::KeyPair& txKeys) {
  m_txKey = txKeys;
  return *this;
}

TransactionBuilder& TransactionBuilder::setInput(const std::vector<cryptonote::tx_source_entry>& sources, const cryptonote::account_keys& senderKeys) {
  m_sources = sources;
  m_senderKeys = senderKeys;
  return *this;
}

TransactionBuilder& TransactionBuilder::addMultisignatureInput(const MultisignatureSource& source) {
  m_msigSources.push_back(source);
  m_version = cryptonote::TRANSACTION_VERSION_2;
  return *this;
}

TransactionBuilder& TransactionBuilder::setOutput(const std::vector<cryptonote::tx_destination_entry>& destinations) {
  m_destinations = destinations;
  return *this;
}

TransactionBuilder& TransactionBuilder::addOutput(const cryptonote::tx_destination_entry& dest) {
  m_destinations.push_back(dest);
  return *this;
}

TransactionBuilder& TransactionBuilder::addMultisignatureOut(uint64_t amount, const KeysVector& keys, uint32_t required, uint32_t term) {

  MultisignatureDestination dst;

  dst.amount = amount;
  dst.keys = keys;
  dst.requiredSignatures = required;
  dst.term = term;

  m_msigDestinations.push_back(dst);
  m_version = cryptonote::TRANSACTION_VERSION_2;

  return *this;
}

Transaction TransactionBuilder::build() const {
  crypto::hash prefixHash;

  Transaction tx;
  add_tx_pub_key_to_extra(tx, m_txKey.pub);

  tx.version = m_version;
  tx.unlockTime = m_unlockTime;

  std::vector<cryptonote::KeyPair> contexts;

  fillInputs(tx, contexts);
  fillOutputs(tx);

  get_transaction_prefix_hash(tx, prefixHash);

  signSources(prefixHash, contexts, tx);

  return tx;
}

void TransactionBuilder::fillInputs(Transaction& tx, std::vector<cryptonote::KeyPair>& contexts) const {
  for (const tx_source_entry& src_entr : m_sources) {
    contexts.push_back(KeyPair());
    KeyPair& in_ephemeral = contexts.back();
    crypto::key_image img;
    generate_key_image_helper(m_senderKeys, src_entr.real_out_tx_key, src_entr.real_output_in_tx_index, in_ephemeral, img);

    // put key image into tx input
    TransactionInputToKey input_to_key;
    input_to_key.amount = src_entr.amount;
    input_to_key.keyImage = img;

    // fill outputs array and use relative offsets
    for (const tx_source_entry::output_entry& out_entry : src_entr.outputs) {
      input_to_key.keyOffsets.push_back(out_entry.first);
    }

    input_to_key.keyOffsets = absolute_output_offsets_to_relative(input_to_key.keyOffsets);
    tx.vin.push_back(input_to_key);
  }

  for (const auto& msrc : m_msigSources) {
    tx.vin.push_back(msrc.input);
  }
}

void TransactionBuilder::fillOutputs(Transaction& tx) const {
  size_t output_index = 0;
  
  for(const auto& dst_entr : m_destinations) {
    crypto::key_derivation derivation;
    crypto::public_key out_eph_public_key;
    crypto::generate_key_derivation(dst_entr.addr.m_viewPublicKey, m_txKey.sec, derivation);
    crypto::derive_public_key(derivation, output_index, dst_entr.addr.m_spendPublicKey, out_eph_public_key);

    TransactionOutput out;
    out.amount = dst_entr.amount;
    TransactionOutputToKey tk;
    tk.key = out_eph_public_key;
    out.target = tk;
    tx.vout.push_back(out);
    output_index++;
  }

  for (const auto& mdst : m_msigDestinations) {   
    TransactionOutput out;
    TransactionOutputMultisignature target;

    target.requiredSignatures = mdst.requiredSignatures;
    target.term = mdst.term;

    for (const auto& key : mdst.keys) {
      crypto::key_derivation derivation;
      crypto::public_key ephemeralPublicKey;
      crypto::generate_key_derivation(key.m_account_address.m_viewPublicKey, m_txKey.sec, derivation);
      crypto::derive_public_key(derivation, output_index, key.m_account_address.m_spendPublicKey, ephemeralPublicKey);
      target.keys.push_back(ephemeralPublicKey);
    }
    out.amount = mdst.amount;
    out.target = target;
    tx.vout.push_back(out);
    output_index++;
  }
}

void TransactionBuilder::setVersion(std::size_t version) {
  m_version = version;
}

void TransactionBuilder::signSources(const crypto::hash& prefixHash, const std::vector<cryptonote::KeyPair>& contexts, Transaction& tx) const {
  
  tx.signatures.clear();

  size_t i = 0;

  // sign TransactionInputToKey sources
  for (const auto& src_entr : m_sources) {
    std::vector<const crypto::public_key*> keys_ptrs;

    for (const auto& o : src_entr.outputs) {
      keys_ptrs.push_back(&o.second);
    }

    tx.signatures.push_back(std::vector<crypto::signature>());
    std::vector<crypto::signature>& sigs = tx.signatures.back();
    sigs.resize(src_entr.outputs.size());
    generate_ring_signature(prefixHash, boost::get<TransactionInputToKey>(tx.vin[i]).keyImage, keys_ptrs, contexts[i].sec, src_entr.real_output, sigs.data());
    i++;
  }

  // sign multisignature source
  for (const auto& msrc : m_msigSources) {
    tx.signatures.resize(tx.signatures.size() + 1);
    auto& outsigs = tx.signatures.back();

    for (const auto& key : msrc.keys) {
      crypto::key_derivation derivation;
      crypto::public_key ephemeralPublicKey;
      crypto::secret_key ephemeralSecretKey;

      crypto::generate_key_derivation(msrc.srcTxPubKey, key.m_view_secret_key, derivation);
      crypto::derive_public_key(derivation, msrc.srcOutputIndex, key.m_account_address.m_spendPublicKey, ephemeralPublicKey);
      crypto::derive_secret_key(derivation, msrc.srcOutputIndex, key.m_spend_secret_key, ephemeralSecretKey);

      crypto::signature sig;
      crypto::generate_signature(prefixHash, ephemeralPublicKey, ephemeralSecretKey, sig);
      outsigs.push_back(sig);
    }
  }
}
