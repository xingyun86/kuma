/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "IOPoll.h"
#include "Notifier.h"
#include "util/kmtrace.h"

#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <sys/poll.h>
#endif

KUMA_NS_BEGIN

class VPoll : public IOPoll
{
public:
    VPoll();
    ~VPoll();
    
    bool init();
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb);
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb);
    int unregisterFd(SOCKET_FD fd);
    int updateFd(SOCKET_FD fd, uint32_t events);
    int wait(uint32_t wait_ms);
    void notify();
    PollType getType() { return POLL_TYPE_POLL; }
    bool isLevelTriggered() { return true; }
    
private:
    uint32_t get_events(uint32_t kuma_events);
    uint32_t get_kuma_events(uint32_t events);
    void resizePollItems(SOCKET_FD fd);
    
private:
    typedef std::vector<pollfd> PollFdVector;
    Notifier        notifier_;
    PollItemVector  poll_items_;
    PollFdVector    poll_fds_;
};

VPoll::VPoll()
{
    
}

VPoll::~VPoll()
{
    poll_fds_.clear();
    poll_items_.clear();
}

bool VPoll::init()
{
    if(!notifier_.init()) {
        return false;
    }
    IOCallback cb ([this] (uint32_t ev) { notifier_.onEvent(ev); });
    registerFd(notifier_.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, std::move(cb));
    return true;
}

uint32_t VPoll::get_events(uint32_t kuma_events)
{
    uint32_t ev = 0;
    if(kuma_events & KUMA_EV_READ) {
        ev |= POLLIN;
#ifndef KUMA_OS_WIN
        ev |= POLLPRI;
#endif
    }
    if(kuma_events & KUMA_EV_WRITE) {
        ev |= POLLOUT;
#ifndef KUMA_OS_WIN
        ev |= POLLWRBAND;
#endif
    }
    if(kuma_events & KUMA_EV_ERROR) {
#ifndef KUMA_OS_WIN
        ev |= POLLERR | POLLHUP | POLLNVAL;
#endif
    }
    return ev;
}

uint32_t VPoll::get_kuma_events(uint32_t events)
{
    uint32_t ev = 0;
    if(events & (POLLIN | POLLPRI)) {
        ev |= KUMA_EV_READ;
    }
    if(events & (POLLOUT | POLLWRBAND)) {
        ev |= KUMA_EV_WRITE;
    }
    if(events & (POLLERR | POLLHUP | POLLNVAL)) {
        ev |= KUMA_EV_ERROR;
    }
    return ev;
}

void VPoll::resizePollItems(SOCKET_FD fd)
{
    if (fd >= poll_items_.size()) {
        poll_items_.resize(fd+1);
    }
}

int VPoll::registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb)
{
    resizePollItems(fd);
    int idx = -1;
    if (INVALID_FD == poll_items_[fd].fd || -1 == poll_items_[fd].idx) { // new
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = get_events(events);
        poll_fds_.push_back(pfd);
        idx = int(poll_fds_.size() - 1);
        poll_items_[fd].idx = idx;
    }
    poll_items_[fd].fd = fd;
    poll_items_[fd].cb = cb;
    KUMA_INFOTRACE("VPoll::registerFd, fd="<<fd<<", events="<<events<<", index="<<idx);
    
    return KUMA_ERROR_NOERR;
}

int VPoll::registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb)
{
    resizePollItems(fd);
    int idx = -1;
    if (INVALID_FD == poll_items_[fd].fd || -1 == poll_items_[fd].idx) { // new
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = get_events(events);
        poll_fds_.push_back(pfd);
        idx = int(poll_fds_.size() - 1);
        poll_items_[fd].idx = idx;
    }
    poll_items_[fd].fd = fd;
    poll_items_[fd].cb = std::move(cb);
    KUMA_INFOTRACE("VPoll::registerFd, fd="<<fd<<", events="<<events<<", index="<<idx);
    
    return KUMA_ERROR_NOERR;
}

int VPoll::unregisterFd(SOCKET_FD fd)
{
    KUMA_INFOTRACE("VPoll::unregisterFd, fd="<<fd);
    int max_fd = int(poll_items_.size() - 1);
    if (fd < 0 || -1 == max_fd || fd > max_fd) {
        KUMA_WARNTRACE("VPoll::unregisterFd, failed, max_fd="<<max_fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    int idx = poll_items_[fd].idx;
    if (fd == max_fd) {
        poll_items_.pop_back();
    } else {
        poll_items_[fd].cb = nullptr;
        poll_items_[fd].fd = INVALID_FD;
        poll_items_[fd].idx = -1;
    }
    
    int last_idx = int(poll_fds_.size() - 1);
    if (idx > last_idx || -1 == idx) {
        return KUMA_ERROR_NOERR;
    }
    if (idx != last_idx) {
        std::iter_swap(poll_fds_.begin()+idx, poll_fds_.end()-1);
        poll_items_[poll_fds_[idx].fd].idx = idx;
    }
    poll_fds_.pop_back();
    return KUMA_ERROR_NOERR;
}

int VPoll::updateFd(SOCKET_FD fd, uint32_t events)
{
    int max_fd = int(poll_items_.size() - 1);
    if (fd < 0 || -1 == max_fd || fd > max_fd) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, fd="<<fd<<", max_fd="<<max_fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    if(poll_items_[fd].fd != fd) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, fd="<<fd<<", item_fd="<<poll_items_[fd].fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    int idx = poll_items_[fd].idx;
    if (idx < 0 || idx >= poll_fds_.size()) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, index="<<idx);
        return KUMA_ERROR_INVALID_STATE;
    }
    if(poll_fds_[idx].fd != fd) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, fd="<<fd<<", pfds_fd="<<poll_fds_[idx].fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    poll_fds_[idx].events = get_events(events);
    return KUMA_ERROR_NOERR;
}

int VPoll::wait(uint32_t wait_ms)
{
#ifdef KUMA_OS_WIN
    int num_revts = WSAPoll(&poll_fds_[0], poll_fds_.size(), wait_ms);
#else
    int num_revts = poll(&poll_fds_[0], poll_fds_.size(), wait_ms);
#endif
    if (-1 == num_revts) {
        if(EINTR == errno) {
            errno = 0;
        } else {
            KUMA_ERRTRACE("VPoll::wait, err="<<getLastError());
        }
        return KUMA_ERROR_INVALID_STATE;
    }

    // copy poll fds since event handler may unregister fd
    PollFdVector poll_fds = poll_fds_;
    
    int idx = 0;
    int last_idx = int(poll_fds.size() - 1);
    while(num_revts > 0 && idx <= last_idx) {
        if(poll_fds[idx].revents) {
            --num_revts;
            if(poll_fds[idx].fd < poll_items_.size()) {
                IOCallback &cb = poll_items_[poll_fds[idx].fd].cb;
                if(cb) cb(get_kuma_events(poll_fds[idx].revents));
            }
        }
        ++idx;
    }
    return KUMA_ERROR_NOERR;
}

void VPoll::notify()
{
    notifier_.notify();
}

IOPoll* createVPoll() {
    return new VPoll();
}

KUMA_NS_END