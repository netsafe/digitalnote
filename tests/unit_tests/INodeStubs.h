// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2014-2015 XDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <vector>
#include <unordered_map>

#include "INode.h"
#include "cryptonote_core/cryptonote_basic.h"
#include "TestBlockchainGenerator.h"
#include "common/ObserverManager.h"
#include "wallet/WalletAsyncContextCounter.h"


class INodeDummyStub : public CryptoNote::INode
{
public:
  virtual bool addObserver(CryptoNote::INodeObserver* observer) override;
  virtual bool removeObserver(CryptoNote::INodeObserver* observer) override;

  virtual void init(const CryptoNote::INode::Callback& callback) {callback(std::error_code());};
  virtual bool shutdown() { return true; };

  virtual size_t getPeerCount() const { return 0; };
  virtual uint64_t getLastLocalBlockHeight() const { return 0; };
  virtual uint64_t getLastKnownBlockHeight() const { return 0; };
  virtual uint64_t getLastLocalBlockTimestamp() const override { return 0; }

  virtual void getNewBlocks(std::list<crypto::hash>&& knownBlockIds, std::list<cryptonote::block_complete_entry>& newBlocks, uint64_t& startHeight, const Callback& callback) {callback(std::error_code());};

  virtual void relayTransaction(const cryptonote::Transaction& transaction, const Callback& callback) {callback(std::error_code());};
  virtual void getRandomOutsByAmounts(std::vector<uint64_t>&& amounts, uint64_t outsCount, std::vector<cryptonote::COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount>& result, const Callback& callback) {callback(std::error_code());};
  virtual void getTransactionOutsGlobalIndices(const crypto::hash& transactionHash, std::vector<uint64_t>& outsGlobalIndices, const Callback& callback) { callback(std::error_code()); };
  virtual void getPoolSymmetricDifference(std::vector<crypto::hash>&& known_pool_tx_ids, crypto::hash known_block_id, bool& is_bc_actual, std::vector<cryptonote::Transaction>& new_txs, std::vector<crypto::hash>& deleted_tx_ids, const Callback& callback) override { is_bc_actual = true; callback(std::error_code()); };
  virtual void queryBlocks(std::list<crypto::hash>&& knownBlockIds, uint64_t timestamp, std::list<CryptoNote::BlockCompleteEntry>& newBlocks, uint64_t& startHeight, const Callback& callback) { callback(std::error_code()); };

  void updateObservers();

  tools::ObserverManager<CryptoNote::INodeObserver> observerManager;

};

class INodeTrivialRefreshStub : public INodeDummyStub
{
public:
  INodeTrivialRefreshStub(TestBlockchainGenerator& generator) : 
    m_lastHeight(1), m_blockchainGenerator(generator), m_nextTxError(false), m_getMaxBlocks(std::numeric_limits<size_t>::max()), m_nextTxToPool(false) {};

  void setGetNewBlocksLimit(size_t maxBlocks) { m_getMaxBlocks = maxBlocks; }

  virtual uint64_t getLastLocalBlockHeight() const { return m_blockchainGenerator.getBlockchain().size() - 1; };
  virtual uint64_t getLastKnownBlockHeight() const { return m_blockchainGenerator.getBlockchain().size() - 1; };

  virtual void getNewBlocks(std::list<crypto::hash>&& knownBlockIds, std::list<cryptonote::block_complete_entry>& newBlocks, uint64_t& startHeight, const Callback& callback);

  virtual void relayTransaction(const cryptonote::Transaction& transaction, const Callback& callback);
  virtual void getRandomOutsByAmounts(std::vector<uint64_t>&& amounts, uint64_t outsCount, std::vector<cryptonote::COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount>& result, const Callback& callback);
  virtual void getTransactionOutsGlobalIndices(const crypto::hash& transactionHash, std::vector<uint64_t>& outsGlobalIndices, const Callback& callback);
  virtual void queryBlocks(std::list<crypto::hash>&& knownBlockIds, uint64_t timestamp, std::list<CryptoNote::BlockCompleteEntry>& newBlocks, uint64_t& startHeight, const Callback& callback) override;
  virtual void getPoolSymmetricDifference(std::vector<crypto::hash>&& known_pool_tx_ids, crypto::hash known_block_id, bool& is_bc_actual,
    std::vector<cryptonote::Transaction>& new_txs, std::vector<crypto::hash>& deleted_tx_ids, const Callback& callback) override;

  virtual void startAlternativeChain(uint64_t height);
  void setNextTransactionError();
  void setNextTransactionToPool();
  void includeTransactionsFromPoolToBlock();

  std::vector<crypto::hash> calls_getTransactionOutsGlobalIndices;

  virtual ~INodeTrivialRefreshStub();

private:
  void doGetNewBlocks(std::list<crypto::hash> knownBlockIds, std::list<cryptonote::block_complete_entry>& newBlocks, uint64_t& startHeight, const Callback& callback);
  void doGetTransactionOutsGlobalIndices(const crypto::hash& transactionHash, std::vector<uint64_t>& outsGlobalIndices, const Callback& callback);
  void doRelayTransaction(const cryptonote::Transaction& transaction, const Callback& callback);
  void doGetRandomOutsByAmounts(std::vector<uint64_t> amounts, uint64_t outsCount, std::vector<cryptonote::COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount>& result, const Callback& callback);
  void doGetPoolSymmetricDifference(std::vector<crypto::hash>& known_pool_tx_ids, crypto::hash known_block_id, bool& is_bc_actual,
    std::vector<cryptonote::Transaction>& new_txs, std::vector<crypto::hash>& deleted_tx_ids, const Callback& callback);

  size_t m_getMaxBlocks;
  uint64_t m_lastHeight;
  TestBlockchainGenerator& m_blockchainGenerator;
  bool m_nextTxError;
  bool m_nextTxToPool;
  std::mutex m_multiWalletLock;
  CryptoNote::WalletAsyncContextCounter m_asyncCounter;
};
