#include "protocol/frameHandler.hpp"
#include "serializer/serializer.hpp"

namespace protocol
{

FrameHandler::FrameHandler()
    : state_(State::IDLE), logger_("FrameHandler"), rxTransmissionOngoing_(false),
      rxLengthKnown_(false), rxNumberReceived_(false), rxControlByteReceived_(false),
      rxPortReceived_(false), rxHeaderReceived_(false), rxCrcBytesReceived_(false), rxLength_(0),
      rxCrc_(0)
{
}

FrameHandler::~FrameHandler()
{
    if (connection_)
    {
        connection_->setHandler(&dispatcher::defaultReader);
    }
}

void FrameHandler::setConnection(dispatcher::IDataReceiver::RawDataReceiverPtr dataReceiver)
{
    connection_ = dataReceiver;
    dataReceiver->setHandler(
        std::bind(&FrameHandler::onRead, this, std::placeholders::_1, std::placeholders::_2));
    logger_.info() << "Connection set";
}

void FrameHandler::connect(u16 port, FrameReceiver frameReceiver)
{
    if (receivers_.count(port) != 0)
    {
        logger_.warn() << "Connection on port " << port << " exists.";
        return;
    }

    receivers_[port] = frameReceiver;
}

void FrameHandler::onRead(const u8* buffer, std::size_t length)
{
    for (int i = 0; i < length; ++i)
    {
        switch (state_)
        {
            case State::IDLE:
            {
                if (FrameByte::Start == buffer[i])
                {
                    rxTransmissionOngoing_ = true;
                    rxLengthKnown_ = false;
                    rxNumberReceived_ = false;
                    rxPortReceived_ = false;
                    rxLength_ = 0;
                    rxCrcBytesReceived_ = 0;
                    rxCrc_ = 0;
                    rxControlByteReceived_ = false;
                    rxHeaderReceived_ = false;
                    rxBuffer_.clear();
                    state_ = State::LENGTH_TRANSMISSION;
                }
            }
            break;

            case State::LENGTH_TRANSMISSION:
            {
                rxLength_ = buffer[i];
                state_ = State::FRAME_NUMBER_TRANSMISSION;
            }
            break;

            case State::FRAME_NUMBER_TRANSMISSION:
            {
                rxBuffer_.number(buffer[i]);
                state_ = State::PORT_TRANSMISSION;
            }
            break;

            case State::PORT_TRANSMISSION:
            {
                rxBuffer_.port(buffer[i]);
                state_ = State::CONTROL_TRANSMISSION;
            }
            break;

            case State::CONTROL_TRANSMISSION:
            {
                rxBuffer_.control(buffer[i]);
                state_ = State::PAYLOAD_TRANSMISSION;
            }
            break;

            case State::PAYLOAD_TRANSMISSION:
            {
                u8 frameBytesToBeReceived = 0;

                // receive payload
                if (rxLength_ != 0)
                {
                    frameBytesToBeReceived = length - i > rxLength_ ? rxLength_ : length - i;
                    rxBuffer_.payload(&buffer[i], frameBytesToBeReceived);
                    rxLength_ -= frameBytesToBeReceived;
                    i += frameBytesToBeReceived - 1;
                }

                if (rxLength_ == 0)
                {
                    state_ = State::CRC_TRANSMISSION;
                }
            }
            break;

            case State::CRC_TRANSMISSION:
            {
                rxCrc_ |= buffer[i] << 8 * rxCrcBytesReceived_;
                logger_.info() << std::to_string(rxCrcBytesReceived_);
                if (++rxCrcBytesReceived_ == 2)
                {
                    state_ = State::END_TRANSMISSION;
                }
            }
            break;

            case State::END_TRANSMISSION:
            {
                if (0 == receivers_.count(rxBuffer_.port()))
                {
                    logger_.error() << "Handler for port " << std::to_string(rxBuffer_.port())
                                    << " not exists.";
                    Frame<0> frame;
                    frame.port(rxBuffer_.port());
                    frame.number(rxBuffer_.number());
                    frame.control(Control::PortNotConnect);
                    send(frame);
                }
                else if (rxBuffer_.crc() != rxCrc_)
                {
                    logger_.error()
                        << "CRC failed. Received " << rxCrc_ << " Expected: " << rxBuffer_.crc()
                        << ", retranssmision requested";
                    Frame<0> frame;
                    frame.port(rxBuffer_.port());
                    frame.number(rxBuffer_.number());
                    frame.control(Control::CrcChecksumFailed);
                    send(frame);
                }
                else if (buffer[i] != FrameByte::End)
                {
                    logger_.error() << "Wrong end received, retranssmision requested";
                    Frame<0> frame;
                    frame.port(rxBuffer_.port());
                    frame.number(rxBuffer_.number());
                    frame.control(Control::WrongEndByte);
                    send(frame);
                }
                else
                {
                    Frame<0> frame;
                    frame.port(rxBuffer_.port());
                    frame.number(rxBuffer_.number());
                    frame.control(Control::Success);
                    send(frame);
                    receivers_.at(rxBuffer_.port())(rxBuffer_);
                }
                state_ = State::IDLE;
            }
            break;
        }
    }
}

void FrameHandler::send(const IFrame& frame)
{
    if (!connection_)
    {
        logger_.error() << "Trying to send message while connection aren't set";
        return;
    }

    connection_->write(FrameByte::Start);
    connection_->write(frame.length());
    connection_->write(frame.number());
    connection_->write(frame.port());
    connection_->write(frame.control());
    connection_->write(frame.payload(), frame.length());
    u8 crc[2];
    serializer::serialize(crc, frame.crc());
    connection_->write(crc, sizeof(crc));
    connection_->write(FrameByte::End);
}


} // namespace protocol