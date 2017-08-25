#pragma once

#include "utils/types.hpp"

namespace message
{

enum TransmissionId : u8
{
    Start = 0x00,
    Ack = 0x10,
    Nack
};

} // namespace message
