/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __H2Request_H__
#define __H2Request_H__

#include "kmdefs.h"
#include "H2ConnectionImpl.h"
#include "IHttpRequest.h"

KUMA_NS_BEGIN

class H2Request : public IHttpRequest
{
public:
    H2Request(EventLoopImpl* loop);
    
    int setSslFlags(uint32_t ssl_flags);
    int sendData(const uint8_t* data, size_t len);
    int close();
    
    int getStatusCode() const { return 200; }
    const std::string& getVersion() const { return strVersionHTTP2_0; }
    const std::string& getHeaderValue(const char* name) const;
    void forEachHeader(EnumrateCallback cb);
    
private:
    void onConnect(int err);
    
    int sendRequest();
    void checkHeaders();
    size_t buildHeaders(HeaderVector &headers);
    void sendSettings();
    void sendHeaders();
    
private:
    EventLoopImpl* loop_;
    H2ConnectionPtr conn_ = nullptr;
    H2StreamPtr stream_ = nullptr;
    
    std::string             connKey_;
};

KUMA_NS_END

#endif /* __H2Request_H__ */
