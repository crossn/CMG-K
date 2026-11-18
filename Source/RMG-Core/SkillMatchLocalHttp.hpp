#ifndef CORE_SKILLMATCHLOCALHTTP_HPP
#define CORE_SKILLMATCHLOCALHTTP_HPP
#include "SkillMatchOutbox.hpp"
#include <cstdint>

// Fixed IPv4 loopback and fixed path; no URL, DNS, proxy or redirect handling.
SkillMatchOutbox::Delivery SkillMatchPostLocal(uint16_t port, const std::string& id,
                                             const std::string& payload);
#endif
