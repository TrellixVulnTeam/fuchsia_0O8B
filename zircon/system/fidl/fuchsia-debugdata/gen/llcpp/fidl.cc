// WARNING: This file is machine generated by fidlgen.

#include <fuchsia/debugdata/llcpp/fidl.h>
#include <memory>

namespace llcpp {

namespace fuchsia {
namespace debugdata {

namespace {

[[maybe_unused]]
constexpr uint64_t kDebugData_Publish_Ordinal = 0x233ab68f00000000lu;
[[maybe_unused]]
constexpr uint64_t kDebugData_Publish_GenOrdinal = 0x32e10c8a45d9312alu;
extern "C" const fidl_type_t fuchsia_debugdata_DebugDataPublishRequestTable;
extern "C" const fidl_type_t fuchsia_debugdata_DebugDataPublishResponseTable;
extern "C" const fidl_type_t v1_fuchsia_debugdata_DebugDataPublishResponseTable;
[[maybe_unused]]
constexpr uint64_t kDebugData_LoadConfig_Ordinal = 0x934ade500000000lu;
[[maybe_unused]]
constexpr uint64_t kDebugData_LoadConfig_GenOrdinal = 0x51012dfe3d37bdf6lu;
extern "C" const fidl_type_t fuchsia_debugdata_DebugDataLoadConfigRequestTable;
extern "C" const fidl_type_t fuchsia_debugdata_DebugDataLoadConfigResponseTable;
extern "C" const fidl_type_t v1_fuchsia_debugdata_DebugDataLoadConfigResponseTable;

}  // namespace

DebugData::ResultOf::Publish_Impl::Publish_Impl(::zx::unowned_channel _client_end, ::fidl::StringView data_sink, ::zx::vmo data) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<PublishRequest, ::fidl::MessageDirection::kSending>();
  std::unique_ptr _write_bytes_boxed = std::make_unique<::fidl::internal::AlignedBuffer<_kWriteAllocSize>>();
  auto& _write_bytes_array = *_write_bytes_boxed;
  PublishRequest _request = {};
  _request.data_sink = std::move(data_sink);
  _request.data = std::move(data);
  auto _linearize_result = ::fidl::Linearize(&_request, _write_bytes_array.view());
  if (_linearize_result.status != ZX_OK) {
    Super::SetFailure(std::move(_linearize_result));
    return;
  }
  ::fidl::DecodedMessage<PublishRequest> _decoded_request = std::move(_linearize_result.message);
  Super::operator=(
      DebugData::InPlace::Publish(std::move(_client_end), std::move(_decoded_request)));
}

DebugData::ResultOf::Publish DebugData::SyncClient::Publish(::fidl::StringView data_sink, ::zx::vmo data) {
    return ResultOf::Publish(::zx::unowned_channel(this->channel_), std::move(data_sink), std::move(data));
}

DebugData::ResultOf::Publish DebugData::Call::Publish(::zx::unowned_channel _client_end, ::fidl::StringView data_sink, ::zx::vmo data) {
  return ResultOf::Publish(std::move(_client_end), std::move(data_sink), std::move(data));
}


DebugData::UnownedResultOf::Publish_Impl::Publish_Impl(::zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::fidl::StringView data_sink, ::zx::vmo data) {
  if (_request_buffer.capacity() < PublishRequest::PrimarySize) {
    Super::status_ = ZX_ERR_BUFFER_TOO_SMALL;
    Super::error_ = ::fidl::internal::kErrorRequestBufferTooSmall;
    return;
  }
  PublishRequest _request = {};
  _request.data_sink = std::move(data_sink);
  _request.data = std::move(data);
  auto _linearize_result = ::fidl::Linearize(&_request, std::move(_request_buffer));
  if (_linearize_result.status != ZX_OK) {
    Super::SetFailure(std::move(_linearize_result));
    return;
  }
  ::fidl::DecodedMessage<PublishRequest> _decoded_request = std::move(_linearize_result.message);
  Super::operator=(
      DebugData::InPlace::Publish(std::move(_client_end), std::move(_decoded_request)));
}

DebugData::UnownedResultOf::Publish DebugData::SyncClient::Publish(::fidl::BytePart _request_buffer, ::fidl::StringView data_sink, ::zx::vmo data) {
  return UnownedResultOf::Publish(::zx::unowned_channel(this->channel_), std::move(_request_buffer), std::move(data_sink), std::move(data));
}

DebugData::UnownedResultOf::Publish DebugData::Call::Publish(::zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::fidl::StringView data_sink, ::zx::vmo data) {
  return UnownedResultOf::Publish(std::move(_client_end), std::move(_request_buffer), std::move(data_sink), std::move(data));
}

::fidl::internal::StatusAndError DebugData::InPlace::Publish(::zx::unowned_channel _client_end, ::fidl::DecodedMessage<PublishRequest> params) {
  DebugData::SetTransactionHeaderFor::PublishRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::internal::StatusAndError::FromFailure(
        std::move(_encode_request_result));
  }
  zx_status_t _write_status =
      ::fidl::Write(std::move(_client_end), std::move(_encode_request_result.message));
  if (_write_status != ZX_OK) {
    return ::fidl::internal::StatusAndError(_write_status, ::fidl::internal::kErrorWriteFailed);
  } else {
    return ::fidl::internal::StatusAndError(ZX_OK, nullptr);
  }
}

template <>
DebugData::ResultOf::LoadConfig_Impl<DebugData::LoadConfigResponse>::LoadConfig_Impl(::zx::unowned_channel _client_end, ::fidl::StringView config_name) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<LoadConfigRequest, ::fidl::MessageDirection::kSending>();
  std::unique_ptr _write_bytes_boxed = std::make_unique<::fidl::internal::AlignedBuffer<_kWriteAllocSize>>();
  auto& _write_bytes_array = *_write_bytes_boxed;
  LoadConfigRequest _request = {};
  _request.config_name = std::move(config_name);
  auto _linearize_result = ::fidl::Linearize(&_request, _write_bytes_array.view());
  if (_linearize_result.status != ZX_OK) {
    Super::SetFailure(std::move(_linearize_result));
    return;
  }
  ::fidl::DecodedMessage<LoadConfigRequest> _decoded_request = std::move(_linearize_result.message);
  Super::SetResult(
      DebugData::InPlace::LoadConfig(std::move(_client_end), std::move(_decoded_request), Super::response_buffer()));
}

DebugData::ResultOf::LoadConfig DebugData::SyncClient::LoadConfig(::fidl::StringView config_name) {
    return ResultOf::LoadConfig(::zx::unowned_channel(this->channel_), std::move(config_name));
}

DebugData::ResultOf::LoadConfig DebugData::Call::LoadConfig(::zx::unowned_channel _client_end, ::fidl::StringView config_name) {
  return ResultOf::LoadConfig(std::move(_client_end), std::move(config_name));
}

template <>
DebugData::UnownedResultOf::LoadConfig_Impl<DebugData::LoadConfigResponse>::LoadConfig_Impl(::zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::fidl::StringView config_name, ::fidl::BytePart _response_buffer) {
  if (_request_buffer.capacity() < LoadConfigRequest::PrimarySize) {
    Super::SetFailure(::fidl::DecodeResult<LoadConfigResponse>(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::internal::kErrorRequestBufferTooSmall));
    return;
  }
  LoadConfigRequest _request = {};
  _request.config_name = std::move(config_name);
  auto _linearize_result = ::fidl::Linearize(&_request, std::move(_request_buffer));
  if (_linearize_result.status != ZX_OK) {
    Super::SetFailure(std::move(_linearize_result));
    return;
  }
  ::fidl::DecodedMessage<LoadConfigRequest> _decoded_request = std::move(_linearize_result.message);
  Super::SetResult(
      DebugData::InPlace::LoadConfig(std::move(_client_end), std::move(_decoded_request), std::move(_response_buffer)));
}

DebugData::UnownedResultOf::LoadConfig DebugData::SyncClient::LoadConfig(::fidl::BytePart _request_buffer, ::fidl::StringView config_name, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::LoadConfig(::zx::unowned_channel(this->channel_), std::move(_request_buffer), std::move(config_name), std::move(_response_buffer));
}

DebugData::UnownedResultOf::LoadConfig DebugData::Call::LoadConfig(::zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::fidl::StringView config_name, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::LoadConfig(std::move(_client_end), std::move(_request_buffer), std::move(config_name), std::move(_response_buffer));
}

::fidl::DecodeResult<DebugData::LoadConfigResponse> DebugData::InPlace::LoadConfig(::zx::unowned_channel _client_end, ::fidl::DecodedMessage<LoadConfigRequest> params, ::fidl::BytePart response_buffer) {
  DebugData::SetTransactionHeaderFor::LoadConfigRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<DebugData::LoadConfigResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<LoadConfigRequest, LoadConfigResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<DebugData::LoadConfigResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}


bool DebugData::TryDispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  if (msg->num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_INVALID_ARGS);
    return true;
  }
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  zx_status_t status = fidl_validate_txn_header(hdr);
  if (status != ZX_OK) {
    txn->Close(status);
    return true;
  }
  switch (hdr->ordinal) {
    case kDebugData_Publish_Ordinal:
    case kDebugData_Publish_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<PublishRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      auto message = result.message.message();
      impl->Publish(std::move(message->data_sink), std::move(message->data),
          Interface::PublishCompleter::Sync(txn));
      return true;
    }
    case kDebugData_LoadConfig_Ordinal:
    case kDebugData_LoadConfig_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<LoadConfigRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      auto message = result.message.message();
      impl->LoadConfig(std::move(message->config_name),
          Interface::LoadConfigCompleter::Sync(txn));
      return true;
    }
    default: {
      return false;
    }
  }
}

bool DebugData::Dispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  bool found = TryDispatch(impl, msg, txn);
  if (!found) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_NOT_SUPPORTED);
  }
  return found;
}


void DebugData::Interface::LoadConfigCompleterBase::Reply(::zx::vmo config) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<LoadConfigResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<LoadConfigResponse*>(_write_bytes);
  DebugData::SetTransactionHeaderFor::LoadConfigResponse(
      ::fidl::DecodedMessage<LoadConfigResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              LoadConfigResponse::PrimarySize,
              LoadConfigResponse::PrimarySize)));
  _response.config = std::move(config);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(LoadConfigResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<LoadConfigResponse>(std::move(_response_bytes)));
}

void DebugData::Interface::LoadConfigCompleterBase::Reply(::fidl::BytePart _buffer, ::zx::vmo config) {
  if (_buffer.capacity() < LoadConfigResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<LoadConfigResponse*>(_buffer.data());
  DebugData::SetTransactionHeaderFor::LoadConfigResponse(
      ::fidl::DecodedMessage<LoadConfigResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              LoadConfigResponse::PrimarySize,
              LoadConfigResponse::PrimarySize)));
  _response.config = std::move(config);
  _buffer.set_actual(sizeof(LoadConfigResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<LoadConfigResponse>(std::move(_buffer)));
}

void DebugData::Interface::LoadConfigCompleterBase::Reply(::fidl::DecodedMessage<LoadConfigResponse> params) {
  DebugData::SetTransactionHeaderFor::LoadConfigResponse(params);
  CompleterBase::SendReply(std::move(params));
}



void DebugData::SetTransactionHeaderFor::PublishRequest(const ::fidl::DecodedMessage<DebugData::PublishRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDebugData_Publish_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}

void DebugData::SetTransactionHeaderFor::LoadConfigRequest(const ::fidl::DecodedMessage<DebugData::LoadConfigRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDebugData_LoadConfig_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
void DebugData::SetTransactionHeaderFor::LoadConfigResponse(const ::fidl::DecodedMessage<DebugData::LoadConfigResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDebugData_LoadConfig_GenOrdinal);
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}

}  // namespace debugdata
}  // namespace fuchsia
}  // namespace llcpp
