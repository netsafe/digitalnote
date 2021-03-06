// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2014-2015 XDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "SynchronizationState.h"

#include "serialization/BinaryInputStreamSerializer.h"
#include "serialization/BinaryOutputStreamSerializer.h"
#include "cryptonote_core/cryptonote_serialization.h"

namespace CryptoNote {

SynchronizationState::ShortHistory SynchronizationState::getShortHistory() const {

  ShortHistory history;
  size_t i = 0;
  size_t current_multiplier = 1;
  size_t sz = m_blockchain.size();

  if (!sz)
    return history;

  size_t current_back_offset = 1;
  bool genesis_included = false;

  while (current_back_offset < sz) {
    history.push_back(m_blockchain[sz - current_back_offset]);
    if (sz - current_back_offset == 0)
      genesis_included = true;
    if (i < 10) {
      ++current_back_offset;
    } else {
      current_back_offset += current_multiplier *= 2;
    }
    ++i;
  }

  if (!genesis_included)
    history.push_back(m_blockchain[0]);

  return history;
}

SynchronizationState::CheckResult SynchronizationState::checkInterval(const BlockchainInterval& interval) const {
  assert(interval.startHeight <= m_blockchain.size());

  CheckResult result = { false, 0, false, 0 };

  size_t intervalEnd = interval.startHeight + interval.blocks.size();
  size_t iterationEnd = std::min(m_blockchain.size(), intervalEnd);

  for (size_t i = interval.startHeight; i < iterationEnd; ++i) {
    if (m_blockchain[i] != interval.blocks[i - interval.startHeight]) {
      result.detachRequired = true;
      result.detachHeight = i;
      break;
    }
  }

  if (result.detachRequired) {
    result.hasNewBlocks = true;
    result.newBlockHeight = result.detachHeight;
    return result;
  }

  if (intervalEnd > m_blockchain.size()) {
    result.hasNewBlocks = true;
    result.newBlockHeight = m_blockchain.size();
  }

  return result;
}

void SynchronizationState::detach(uint64_t height) {
  assert(height < m_blockchain.size());
  m_blockchain.resize(height);
}

void SynchronizationState::addBlocks(const crypto::hash* blockHashes, uint64_t height, size_t count) {
  assert(blockHashes);
  auto size = m_blockchain.size();
  assert( size == height);
  m_blockchain.insert(m_blockchain.end(), blockHashes, blockHashes + count);
}

uint64_t SynchronizationState::getHeight() const {
  return m_blockchain.size();
}

void SynchronizationState::save(std::ostream& os) {
  cryptonote::BinaryOutputStreamSerializer s(os);
  serialize(s, "state");
}

void SynchronizationState::load(std::istream& in) {
  cryptonote::BinaryInputStreamSerializer s(in);
  serialize(s, "state");
}

cryptonote::ISerializer& SynchronizationState::serialize(cryptonote::ISerializer& s, const std::string& name) {
  s.beginObject(name);
  s(m_blockchain, "blockchain");
  s.endObject();
  return s;
}

}
