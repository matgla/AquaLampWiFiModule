#include "protocol/frameHandler.hpp"

#include <boost/core/ignore_unused.hpp>
#include <gsl/span>

#include "IFrame.hpp"
#include "dispatcher/IDataReceiver.hpp"
#include "frame.hpp"
#include "protocol/messages/control.hpp"
#include "serializer/serializer.hpp"

namespace protocol
{

FrameHandler::FrameHandler()
    : state_{State::IDLE}, rxCrcBytesReceived_{0}, rxLength_{0}, rxCrc_{0}, logger_("FrameHandler")
{
}

FrameHandler::~FrameHandler()
{
    if (connection_)
    {
        connection_->setHandler(&defaultReader);
    }
}

void FrameHandler::setConnection(const dispatcher::IDataReceiver::RawDataReceiverPtr& dataReceiver)
{
    connection_ = dataReceiver;
    dataReceiver->setHandler(std::bind(&FrameHandler::onRead, this, std::placeholders::_1,
                                       std::placeholders::_2));

    logger_.info() << "Connection set";
}

void FrameHandler::connect(const u16 port, const FrameReceiver& frameReceiver)
{
    if (receivers_.count(port) != 0)
    {
        logger_.warn() << "Connection on port " << port << " exists.";
        return;
    }

    receivers_[port] = frameReceiver;
}

void FrameHandler::sendReply(const messages::Control status)
{
    Frame<0> frame;
    frame.port(rxBuffer_.port());
    frame.number(rxBuffer_.number());
    frame.control(status);
    send(frame);
}

void FrameHandler::onRead(const BufferSpan& buffer,
                          const WriterCallback& writer)
{
    boost::ignore_unused(writer);

    for (auto i = 0; i < buffer.length(); ++i)
    {
        switch (state_)
        {
            case State::IDLE:
            {
                if (FrameByte::Start == buffer[i])
                {
                    rxLength_ = 0;
                    rxCrcBytesReceived_ = 0;
                    rxCrc_ = 0;
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
                if (rxLength_ > 0)
                {
                    state_ = State::PAYLOAD_TRANSMISSION;
                }
                else
                {
                    state_ = State::CRC_TRANSMISSION;
                }
            }
            break;

            case State::PAYLOAD_TRANSMISSION:
            {
                u8 frameBytesToBeReceived = 0;

                // receive payload
                if (rxLength_ != 0)
                {
                    frameBytesToBeReceived = buffer.length() - i > rxLength_ ? rxLength_ : buffer.length() - i;
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
                    sendReply(messages::Control::PortNotConnect);
                }
                else if (rxBuffer_.crc() != rxCrc_)
                {
                    logger_.error()
                        << "CRC failed. Received " << rxCrc_ << " Expected: " << rxBuffer_.crc()
                        << ", retranssmision requested";
                    sendReply(messages::Control::CrcChecksumFailed);
                }
                else if (buffer[i] != FrameByte::End)
                {
                    logger_.error() << "Wrong end byte received";
                    sendReply(messages::Control::WrongEndByte);
                }
                else
                {
                    if (rxBuffer_.control() == messages::Control::Transmission)
                    {
                        sendReply(messages::Control::Success);
                    }
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
    connection_->write(BufferSpan{ frame.payload(), frame.payloadSize() });
    u8 crc[2];
    serializer::serialize(static_cast<u8*>(crc), frame.crc());
    connection_->write(gsl::span<const u8>{crc});
    connection_->write(FrameByte::End);
}


} // namespace protocol
