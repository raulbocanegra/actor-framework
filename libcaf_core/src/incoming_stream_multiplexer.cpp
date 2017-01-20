/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2016                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/detail/incoming_stream_multiplexer.hpp"

#include "caf/send.hpp"
#include "caf/variant.hpp"
#include "caf/to_string.hpp"
#include "caf/local_actor.hpp"

namespace caf {
namespace detail {

incoming_stream_multiplexer::incoming_stream_multiplexer(local_actor* self,
                                                         backend& service,
                                                         actor& basp)
    : stream_multiplexer(self, service, basp) {
  // nop
}

void incoming_stream_multiplexer::operator()(stream_msg& x) {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_LOG_TRACE(CAF_ARG(x));
  CAF_ASSERT(self_->current_mailbox_element() != nullptr);
  dispatch(*this, x);
}

void incoming_stream_multiplexer::operator()(stream_msg::open& x) {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_LOG_TRACE(CAF_ARG(x));
  CAF_ASSERT(current_stream_msg_ != nullptr);
  auto prev = std::move(x.prev_stage);
  // Make sure we have a previous stage.
  if (!prev) {
    CAF_LOG_WARNING("received stream_msg::open without previous stage");
    return fail(sec::invalid_upstream, nullptr);
  }
  // Make sure we have a next stage.
  auto cme = self_->current_mailbox_element();
  if (!cme || cme->stages.empty()) {
    CAF_LOG_WARNING("received stream_msg::open without next stage");
    return fail(sec::invalid_downstream, std::move(prev));
  }
  auto successor = cme->stages.back();
  cme->stages.pop_back();
  // Our prev always is the remote stream server proxy.
  auto current_remote_path = remotes_.emplace(prev->node(), prev).first;
  current_stream_state_ = streams_.emplace(current_stream_msg_->sid,
                                           stream_state{std::move(prev),
                                           successor,
                                           &current_remote_path->second}).first;
  // Rewrite handshake and forward it to the next stage.
  x.prev_stage = self_->ctrl();
  send_local(successor, std::move(*current_stream_msg_),
             std::move(cme->stages), cme->mid);
  // Send out demand upstream.
  manage_credit();
}

void incoming_stream_multiplexer::operator()(stream_msg::ack_open&) {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_ASSERT(current_stream_msg_ != nullptr);
  CAF_ASSERT(current_stream_state_ != streams_.end());
  forward_to_upstream();
}

void incoming_stream_multiplexer::operator()(stream_msg::batch&) {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_ASSERT(current_stream_msg_ != nullptr);
  CAF_ASSERT(current_stream_state_ != streams_.end());
  forward_to_downstream();
}

void incoming_stream_multiplexer::operator()(stream_msg::ack_batch&) {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_ASSERT(current_stream_msg_ != nullptr);
  CAF_ASSERT(current_stream_state_ != streams_.end());
  forward_to_upstream();
}

void incoming_stream_multiplexer::operator()(stream_msg::close&) {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_ASSERT(current_stream_msg_ != nullptr);
  CAF_ASSERT(current_stream_state_ != streams_.end());
  auto i = streams_.find(current_stream_msg_->sid);
  if (i != streams_.end()) {
    forward_to_downstream();
    streams_.erase(i);
  }
}

void incoming_stream_multiplexer::operator()(stream_msg::abort& x) {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_ASSERT(current_stream_msg_ != nullptr);
  CAF_ASSERT(current_stream_state_ != streams_.end());
  auto i = streams_.find(current_stream_msg_->sid);
  if (i != streams_.end()) {
    if (i->second.prev_stage == self_->current_sender())
      fail(x.reason, nullptr, i->second.next_stage);
    else
      fail(x.reason, i->second.prev_stage);
    streams_.erase(i);
  }
}

void incoming_stream_multiplexer::operator()(stream_msg::downstream_failed&) {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_ASSERT(current_stream_msg_ != nullptr);
  CAF_ASSERT(current_stream_state_ != streams_.end());
}

void incoming_stream_multiplexer::operator()(stream_msg::upstream_failed&) {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_ASSERT(current_stream_msg_ != nullptr);
  CAF_ASSERT(current_stream_state_ != streams_.end());
}

void incoming_stream_multiplexer::forward_to_upstream() {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_ASSERT(current_stream_msg_ != nullptr);
  CAF_ASSERT(current_stream_state_ != streams_.end());
  send_remote(*current_stream_state_->second.rpath,
              std::move(*current_stream_msg_));
}

void incoming_stream_multiplexer::forward_to_downstream() {
printf("%s %d\n", __FILE__, __LINE__);
  CAF_ASSERT(current_stream_msg_ != nullptr);
  // When forwarding downstream, we also have to manage upstream credit.
  manage_credit();
  send_local(current_stream_state_->second.next_stage,
             std::move(*current_stream_msg_));
}

} // namespace detail
} // namespace caf

