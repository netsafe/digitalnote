// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2014-2015 XDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gtest/gtest.h"
#include <tuple>

#include "cryptonote_core/TransactionApi.h"
#include "transfers/TransfersSubscription.h"
#include "transfers/TypeHelpers.h"
#include "ITransfersContainer.h"

#include "TransactionApiHelpers.h"
#include "TransfersObserver.h"

using namespace CryptoNote;
using namespace cryptonote;

namespace {

const uint64_t UNCONFIRMED = std::numeric_limits<uint64_t>::max();

std::error_code createError() {
  return std::make_error_code(std::errc::invalid_argument);
}


class TransfersSubscriptionTest : public ::testing::Test {
public:

  TransfersSubscriptionTest() :
    currency(CurrencyBuilder().currency()),
    account(generateAccountKeys()),
    syncStart(SynchronizationStart{ 0, 0 }),
    sub(currency, AccountSubscription{ account, syncStart, 10 }) {
    sub.addObserver(&observer);
  }

  std::unique_ptr<ITransaction> addTransaction(uint64_t amount, uint64_t height, uint64_t outputIndex) {
    auto tx = createTransaction();
    addTestInput(*tx, amount);
    auto outInfo = addTestKeyOutput(*tx, amount, outputIndex, account);
    std::vector<TransactionOutputInformationIn> outputs = { outInfo };
    sub.addTransaction(BlockInfo{ height, 100000 }, *tx, outputs, {});
    return tx;
  }

  Currency currency;
  AccountKeys account;
  SynchronizationStart syncStart;
  TransfersSubscription sub;
  TransfersObserver observer;
};
}



TEST_F(TransfersSubscriptionTest, getInitParameters) {
  ASSERT_EQ(syncStart.height, sub.getSyncStart().height);
  ASSERT_EQ(syncStart.timestamp, sub.getSyncStart().timestamp);
  ASSERT_EQ(account.address, sub.getAddress());
  ASSERT_EQ(account, sub.getKeys());
}

TEST_F(TransfersSubscriptionTest, addTransaction) {
  auto tx1 = addTransaction(10000, 1, 0);
  auto tx2 = addTransaction(10000, 2, 1);

  // this transaction should not be added, so no notification
  auto tx = createTransaction();
  addTestInput(*tx, 20000);
  sub.addTransaction(BlockInfo{ 2, 100000 }, *tx, {}, {});

  ASSERT_EQ(2, sub.getContainer().transactionsCount());
  ASSERT_EQ(2, observer.updated.size());
  ASSERT_EQ(tx1->getTransactionHash(), observer.updated[0]);
  ASSERT_EQ(tx2->getTransactionHash(), observer.updated[1]);
}

TEST_F(TransfersSubscriptionTest, onBlockchainDetach) {
  addTransaction(10000, 10, 0);
  auto txHash = addTransaction(10000, 11, 1)->getTransactionHash();
  ASSERT_EQ(2, sub.getContainer().transactionsCount());
  
  sub.onBlockchainDetach(11);

  ASSERT_EQ(1, sub.getContainer().transactionsCount());
  ASSERT_EQ(1, observer.deleted.size());
  ASSERT_EQ(txHash, observer.deleted[0]);
}

TEST_F(TransfersSubscriptionTest, onError) {

  auto err = createError();

  addTransaction(10000, 10, 0);
  addTransaction(10000, 11, 1);

  ASSERT_EQ(2, sub.getContainer().transactionsCount());

  sub.onError(err, 12);

  ASSERT_EQ(2, sub.getContainer().transactionsCount());
  ASSERT_EQ(1, observer.errors.size());
  ASSERT_EQ(std::make_tuple(12, err), observer.errors[0]);

  sub.onError(err, 11);

  ASSERT_EQ(1, sub.getContainer().transactionsCount()); // one transaction should be detached
  ASSERT_EQ(2, observer.errors.size());

  ASSERT_EQ(std::make_tuple(12, err), observer.errors[0]);
  ASSERT_EQ(std::make_tuple(11, err), observer.errors[1]);
}

TEST_F(TransfersSubscriptionTest, advanceHeight) {
  ASSERT_TRUE(sub.advanceHeight(10));
  ASSERT_FALSE(sub.advanceHeight(9)); // can't go backwards
}


TEST_F(TransfersSubscriptionTest, markTransactionConfirmed) {
  auto txHash = addTransaction(10000, UNCONFIRMED, UNCONFIRMED)->getTransactionHash();
  ASSERT_EQ(1, sub.getContainer().transactionsCount());
  ASSERT_EQ(1, observer.updated.size()); // added

  sub.markTransactionConfirmed(BlockInfo{ 10, 100000 }, txHash, { 1 });

  ASSERT_EQ(2, observer.updated.size()); // added + updated
  ASSERT_EQ(txHash, observer.updated[0]);
}

TEST_F(TransfersSubscriptionTest, deleteUnconfirmedTransaction) {
  auto txHash = addTransaction(10000, UNCONFIRMED, UNCONFIRMED)->getTransactionHash();
  ASSERT_EQ(1, sub.getContainer().transactionsCount());

  sub.deleteUnconfirmedTransaction(txHash);

  ASSERT_EQ(0, sub.getContainer().transactionsCount());
  ASSERT_EQ(1, observer.deleted.size());
  ASSERT_EQ(txHash, observer.deleted[0]);
}

