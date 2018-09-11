/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013-2017 Regents of the University of California.
 *
 * This file is part of ndn-cxx library (NDN C++ library with eXperimental eXtensions).
 *
 * ndn-cxx library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * ndn-cxx library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and GNU Lesser
 * General Public License along with ndn-cxx, e.g., in COPYING.md file.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndn-cxx authors and contributors.
 */

#include "interest.hpp"
#include "util/random.hpp"
#include "data.hpp"

#include <cstring>
#include <sstream>

namespace ndn {

BOOST_CONCEPT_ASSERT((boost::EqualityComparable<Interest>));
BOOST_CONCEPT_ASSERT((WireEncodable<Interest>));
BOOST_CONCEPT_ASSERT((WireEncodableWithEncodingBuffer<Interest>));
BOOST_CONCEPT_ASSERT((WireDecodable<Interest>));
static_assert(std::is_base_of<tlv::Error, Interest::Error>::value,
              "Interest::Error must inherit from tlv::Error");

Interest::Interest(const Name& name, time::milliseconds interestLifetime)
  : m_name(name)
  , m_interestLifetime(interestLifetime)
{
  if (interestLifetime < time::milliseconds::zero()) {
    BOOST_THROW_EXCEPTION(std::invalid_argument("InterestLifetime must be >= 0"));
  }
}

Interest::Interest(const Block& wire)
{
  wireDecode(wire);
}

// ---- encode and decode ----
template<::ndn::encoding::Tag TAG>
size_t
Interest::wireEncodeAccNames(EncodingImpl<TAG>& encoder) const
{
	size_t totalLength = 0;
	//LogManager::AddLogWithNodeId("interest.cpp->wireEncodeAccNames.start");
	for (auto it = m_accNames.rbegin(); it != m_accNames.rend(); it++) {
		//LogManager::AddLogWithNodeId("interest.cpp->wireEncodeAccNames.1");
		totalLength += (*it).wireEncode(encoder);
		//LogManager::AddLogWithNodeId("interest.cpp->wireEncodeAccNames.2");
	}
	//LogManager::AddLogWithNodeId("interest.cpp->wireEncodeAccNames.3");

	totalLength += encoder.prependVarNumber(totalLength);
	totalLength += encoder.prependVarNumber(::ndn::tlv::AccumulativeNames);
	//LogManager::AddLogWithNodeId("interest.cpp->wireEncodeAccNames.completed");
	return totalLength;
}


template<encoding::Tag TAG>
size_t
Interest::wireEncode(EncodingImpl<TAG>& encoder) const
{
  size_t totalLength = 0;

  // Interest ::= INTEREST-TYPE TLV-LENGTH
  //                Name
  //                Selectors?
  //                Nonce
  //                InterestLifetime?
  //                ForwardingHint?

  // (reverse encoding)

  // ForwardingHint
  if (m_forwardingHint.size() > 0) {
    totalLength += m_forwardingHint.wireEncode(encoder);
  }

  // InterestLifetime
  if (getInterestLifetime() != DEFAULT_INTEREST_LIFETIME) {
    totalLength += prependNonNegativeIntegerBlock(encoder,
                                                  tlv::InterestLifetime,
                                                  getInterestLifetime().count());
  }

  // Nonce
  uint32_t nonce = this->getNonce(); // assigns random Nonce if needed
  totalLength += encoder.prependByteArray(reinterpret_cast<uint8_t*>(&nonce), sizeof(nonce));
  totalLength += encoder.prependVarNumber(sizeof(nonce));
  totalLength += encoder.prependVarNumber(tlv::Nonce);

  // Previous Node Id
  uint32_t previousNodeID = this->getPreviousNodeId();
	totalLength += encoder.prependByteArray(reinterpret_cast<uint8_t*>(&previousNodeID), sizeof(previousNodeID));
	totalLength += encoder.prependVarNumber(sizeof(previousNodeID));
	totalLength += encoder.prependVarNumber(tlv::PreviousNodeId);

	// Accumulative Names
	totalLength += wireEncodeAccNames(encoder);


  // Selectors
  if (hasSelectors()) {
    totalLength += getSelectors().wireEncode(encoder);
  }

  // Name
  totalLength += getName().wireEncode(encoder);

  totalLength += encoder.prependVarNumber(totalLength);
  totalLength += encoder.prependVarNumber(tlv::Interest);
  return totalLength;
}

NDN_CXX_DEFINE_WIRE_ENCODE_INSTANTIATIONS(Interest);

const Block&
Interest::wireEncode() const
{
	/*
	 * Commented this if-condition so that Interest will be wireEncoded every time
	 *
  if (m_wire.hasWire())
    return m_wire;
	 */

  EncodingEstimator estimator;
  size_t estimatedSize = wireEncode(estimator);

  EncodingBuffer buffer(estimatedSize, 0);
  wireEncode(buffer);

  const_cast<Interest*>(this)->wireDecode(buffer.block());
  return m_wire;
}

std::vector<Name>
Interest::wireDecodeAccNames(const Block& wire) {
	//LogManager::AddLogWithNodeId("interest.cpp->wireDecodeAccNames.start");
	std::vector<Name> v;
	const Block& content = wire; //m_wire.find(tlv::AccumulativeNames);
	content.parse();
	auto element = content.elements_begin();
	if (content.elements_end() == element) {
		//LogManager::AddLogWithNodeId("interest.cpp->wireDecodeAccNames.content.empty.return");
		return v;
	}

	for (; element != content.elements_end(); ++element) {
		element->parse();
		Name packetName(*element);
		if (packetName.empty()) {
			//LogManager::AddLogWithNodeId("interest.cpp->wireDecodeAccNames.packetName.empty.return");
			return v;
		}

		//LogManager::AddLogWithNodeId("interest.cpp->wireDecodeAccNames.packetName", packetName.toUri());
		v.push_back(packetName);
	}

	//LogManager::AddLogWithNodeId("interest.cpp->wireDecodeAccNames.completed.count", (int)v.size());
	return v;
}

void
Interest::wireDecode(const Block& wire)
{
  m_wire = wire;
  m_wire.parse();

  if (m_wire.type() != tlv::Interest)
    BOOST_THROW_EXCEPTION(Error("Unexpected TLV number when decoding Interest"));

  // Name
  m_name.wireDecode(m_wire.get(tlv::Name));

  // Selectors
  Block::element_const_iterator val = m_wire.find(tlv::Selectors);
  if (val != m_wire.elements_end()) {
    m_selectors.wireDecode(*val);
  }
  else
    m_selectors = Selectors();


  // Previous Node Id
    val = m_wire.find(tlv::PreviousNodeId);
    if (val == m_wire.elements_end()) {
      BOOST_THROW_EXCEPTION(Error("Previous Node ID element is missing"));
    }
    uint32_t previousNodeId = 0;
    if (val->value_size() != sizeof(previousNodeId)) {
      BOOST_THROW_EXCEPTION(Error("Previous Node ID element is malformed"));
    }
    std::memcpy(&previousNodeId, val->value(), sizeof(previousNodeId));
    m_previousNodeId = previousNodeId;

    // Accumulative Names
    val = m_wire.find(tlv::AccumulativeNames);
    if (val == m_wire.elements_end()) {
	  BOOST_THROW_EXCEPTION(Error("Accumulative Names element is missing"));
	}
    std::vector<Name> v = wireDecodeAccNames(*val);
    m_accNames = v;

  // Nonce
  val = m_wire.find(tlv::Nonce);
  if (val == m_wire.elements_end()) {
    BOOST_THROW_EXCEPTION(Error("Nonce element is missing"));
  }
  uint32_t nonce = 0;
  if (val->value_size() != sizeof(nonce)) {
    BOOST_THROW_EXCEPTION(Error("Nonce element is malformed"));
  }
  std::memcpy(&nonce, val->value(), sizeof(nonce));
  m_nonce = nonce;

  // InterestLifetime
  val = m_wire.find(tlv::InterestLifetime);
  if (val != m_wire.elements_end()) {
    m_interestLifetime = time::milliseconds(readNonNegativeInteger(*val));
  }
  else {
    m_interestLifetime = DEFAULT_INTEREST_LIFETIME;
  }

  // ForwardingHint
  val = m_wire.find(tlv::ForwardingHint);
  if (val != m_wire.elements_end()) {
    m_forwardingHint.wireDecode(*val, false);
  }
  else {
    m_forwardingHint = DelegationList();
  }
}

std::string
Interest::toUri() const
{
  std::ostringstream os;
  os << *this;
  return os.str();
}

// ---- matching ----

bool
Interest::matchesName(const Name& name) const
{
  if (name.size() < m_name.size())
    return false;

  if (!m_name.isPrefixOf(name))
    return false;

  if (getMinSuffixComponents() >= 0 &&
      // name must include implicit digest
      !(name.size() - m_name.size() >= static_cast<size_t>(getMinSuffixComponents())))
    return false;

  if (getMaxSuffixComponents() >= 0 &&
      // name must include implicit digest
      !(name.size() - m_name.size() <= static_cast<size_t>(getMaxSuffixComponents())))
    return false;

  if (!getExclude().empty() &&
      name.size() > m_name.size() &&
      getExclude().isExcluded(name[m_name.size()]))
    return false;

  return true;
}

bool
Interest::matchesData(const Data& data) const
{
  size_t interestNameLength = m_name.size();
  const Name& dataName = data.getName();
  size_t fullNameLength = dataName.size() + 1;

  // check MinSuffixComponents
  bool hasMinSuffixComponents = getMinSuffixComponents() >= 0;
  size_t minSuffixComponents = hasMinSuffixComponents ?
                               static_cast<size_t>(getMinSuffixComponents()) : 0;
  if (!(interestNameLength + minSuffixComponents <= fullNameLength))
    return false;

  // check MaxSuffixComponents
  bool hasMaxSuffixComponents = getMaxSuffixComponents() >= 0;
  if (hasMaxSuffixComponents &&
      !(interestNameLength + getMaxSuffixComponents() >= fullNameLength))
    return false;

  // check prefix
  if (interestNameLength == fullNameLength) {
    if (m_name.get(-1).isImplicitSha256Digest()) {
      if (m_name != data.getFullName())
        return false;
    }
    else {
      // Interest Name is same length as Data full Name, but last component isn't digest
      // so there's no possibility of matching
      return false;
    }
  }
  else {
    // Interest Name is a strict prefix of Data full Name
    if (!m_name.isPrefixOf(dataName))
      return false;
  }

  // check Exclude
  // Exclude won't be violated if Interest Name is same as Data full Name
  if (!getExclude().empty() && fullNameLength > interestNameLength) {
    if (interestNameLength == fullNameLength - 1) {
      // component to exclude is the digest
      if (getExclude().isExcluded(data.getFullName().get(interestNameLength)))
        return false;
      // There's opportunity to inspect the Exclude filter and determine whether
      // the digest would make a difference.
      // eg. "<NameComponent>AA</NameComponent><Any/>" doesn't exclude any digest -
      //     fullName not needed;
      //     "<Any/><NameComponent>AA</NameComponent>" and
      //     "<Any/><ImplicitSha256DigestComponent>ffffffffffffffffffffffffffffffff
      //      </ImplicitSha256DigestComponent>"
      //     excludes all digests - fullName not needed;
      //     "<Any/><ImplicitSha256DigestComponent>80000000000000000000000000000000
      //      </ImplicitSha256DigestComponent>"
      //     excludes some digests - fullName required
      // But Interests that contain the exact Data Name before digest and also
      // contain Exclude filter is too rare to optimize for, so we request
      // fullName no matter what's in the Exclude filter.
    }
    else {
      // component to exclude is not the digest
      if (getExclude().isExcluded(dataName.get(interestNameLength)))
        return false;
    }
  }

  // check PublisherPublicKeyLocator
  const KeyLocator& publisherPublicKeyLocator = this->getPublisherPublicKeyLocator();
  if (!publisherPublicKeyLocator.empty()) {
    const Signature& signature = data.getSignature();
    const Block& signatureInfo = signature.getInfo();
    Block::element_const_iterator it = signatureInfo.find(tlv::KeyLocator);
    if (it == signatureInfo.elements_end()) {
      return false;
    }
    if (publisherPublicKeyLocator.wireEncode() != *it) {
      return false;
    }
  }

  return true;
}

bool
Interest::matchesInterest(const Interest& other) const
{
  /// @todo #3162 match ForwardingHint field
  return (this->getName() == other.getName() &&
          this->getSelectors() == other.getSelectors());
}

// ---- field accessors ----

uint32_t
Interest::getNonce() const
{
  if (!m_nonce) {
    m_nonce = random::generateWord32();
  }
  return *m_nonce;
}

Interest&
Interest::setNonce(uint32_t nonce)
{
  m_nonce = nonce;
  m_wire.reset();
  return *this;
}

void
Interest::refreshNonce()
{
  if (!hasNonce())
    return;

  uint32_t oldNonce = getNonce();
  uint32_t newNonce = oldNonce;
  while (newNonce == oldNonce)
    newNonce = random::generateWord32();

  setNonce(newNonce);
}

Interest&
Interest::setInterestLifetime(time::milliseconds interestLifetime)
{
  if (interestLifetime < time::milliseconds::zero()) {
    BOOST_THROW_EXCEPTION(std::invalid_argument("InterestLifetime must be >= 0"));
  }
  m_interestLifetime = interestLifetime;
  m_wire.reset();
  return *this;
}

Interest&
Interest::setForwardingHint(const DelegationList& value)
{
  m_forwardingHint = value;
  m_wire.reset();
  return *this;
}

// AIFBS
Interest&
Interest::setPreviousNodeId(const uint32_t id) {
	m_previousNodeId = id;
	m_wire.reset();
	return *this;
}

uint32_t
Interest::getPreviousNodeId() const {
	return m_previousNodeId;
}

Interest&
Interest::setAccNames(const std::vector<Name> v) {
	m_accNames = v;
	m_wire.reset();
	return *this;
}

std::vector<Name>
Interest::getAccNames() const {
	return m_accNames;
}


// ---- operators ----

std::ostream&
operator<<(std::ostream& os, const Interest& interest)
{
  os << interest.getName();

  char delim = '?';

  if (interest.getMinSuffixComponents() >= 0) {
    os << delim << "ndn.MinSuffixComponents=" << interest.getMinSuffixComponents();
    delim = '&';
  }
  if (interest.getMaxSuffixComponents() >= 0) {
    os << delim << "ndn.MaxSuffixComponents=" << interest.getMaxSuffixComponents();
    delim = '&';
  }
  if (interest.getChildSelector() != DEFAULT_CHILD_SELECTOR) {
    os << delim << "ndn.ChildSelector=" << interest.getChildSelector();
    delim = '&';
  }
  if (interest.getMustBeFresh()) {
    os << delim << "ndn.MustBeFresh=" << interest.getMustBeFresh();
    delim = '&';
  }
  if (interest.getInterestLifetime() != DEFAULT_INTEREST_LIFETIME) {
    os << delim << "ndn.InterestLifetime=" << interest.getInterestLifetime().count();
    delim = '&';
  }

  if (interest.hasNonce()) {
    os << delim << "ndn.Nonce=" << interest.getNonce();
    delim = '&';
  }
  if (!interest.getExclude().empty()) {
    os << delim << "ndn.Exclude=" << interest.getExclude();
    delim = '&';
  }

  //Previous Node Id
  if (!interest.getPreviousNodeId() != 0) {
	os << delim << "ndn.PreviousNodeId=" << interest.getPreviousNodeId();
	delim = '&';
  }

  //Accumulative Names
  if(interest.getAccNames().size() != 0){
	  std::string names = "";
	  std::vector<Name> v = interest.getAccNames();
	  for(unsigned int k = 0; k < v.size(); k++)
		  names += v[k].toUri() + "+";

	  if(names.size() > 0)
		  names = names.substr(0, names.size()-1);

	  os << delim << "ndn.AccNames=" << names;
	  delim = '&';
  }


  return os;
}

} // namespace ndn
