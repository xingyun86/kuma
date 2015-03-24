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

#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
# include <windows.h>
#else
# include <string.h>
# include <netdb.h>
# include <arpa/inet.h>
# include <fcntl.h>
# include <errno.h>
# include <sys/types.h>
# include <sys/time.h>
#endif

#ifdef KUMA_HAS_CXX0X
#include <chrono>
#include <random>
#endif

KUMA_NS_BEGIN

#ifdef KUMA_OS_WIN
#define STRNCPY_S   strncpy_s
#define SNPRINTF(d, dl, fmt, ...)    _snprintf_s(d, dl, _TRUNCATE, fmt, ##__VA_ARGS__)
#else
#define STRNCPY_S(d, dl, s, sl) \
do{ \
    if(0 == dl) \
        break; \
    strncpy(d, s, dl-1); \
    d[dl-1]='\0'; \
}while(0);
#define SNPRINTF    snprintf
#endif

enum{
    KM_RESOLVE_IPV0    = 0,
    KM_RESOLVE_IPV4    = 1,
    KM_RESOLVE_IPV6    = 2
};

#ifdef KUMA_OS_WIN
typedef int (WSAAPI *pf_getaddrinfo)(
    _In_opt_  PCSTR pNodeName,
    _In_opt_  PCSTR pServiceName,
    _In_opt_  const ADDRINFOA *pHints,
    _Out_     PADDRINFOA *ppResult
);

typedef int (WSAAPI *pf_getnameinfo)(
    __in   const struct sockaddr FAR *sa,
    __in   socklen_t salen,
    __out  char FAR *host,
    __in   DWORD hostlen,
    __out  char FAR *serv,
    __in   DWORD servlen,
    __in   int flags
);

typedef void (WSAAPI *pf_freeaddrinfo)(
    __in  struct addrinfo *ai
);

static HMODULE s_hmod_ws2_32 = NULL;
static bool s_ipv6_support = false;
static bool s_ipv6_inilized = false;
pf_getaddrinfo km_getaddrinfo = NULL;
pf_getnameinfo km_getnameinfo = NULL;
pf_freeaddrinfo km_freeaddrinfo = NULL;
#else
typedef int (*pf_getaddrinfo)(
    const char *node,
    const char *service,
    const struct addrinfo *hints,
    struct addrinfo **res
);

typedef int (*pf_getnameinfo)(
    const struct sockaddr *sa,
    socklen_t salen,
    char *host,
    socklen_t hostlen,
    char *serv,
    socklen_t servlen,
#ifdef KUMA_OS_LINUX
    int flags
#else
    int flags
#endif
);

typedef void (*pf_freeaddrinfo)(
    struct addrinfo *res
);

pf_getaddrinfo km_getaddrinfo = getaddrinfo;
pf_getnameinfo km_getnameinfo = getnameinfo;
pf_freeaddrinfo km_freeaddrinfo = freeaddrinfo;
#endif

bool ipv6_api_init()
{
#ifdef KUMA_OS_WIN
    if(!s_ipv6_inilized)
    {
        s_ipv6_inilized = true;
        
#define GET_WS2_FUNCTION(f) km_##f = (pf_##f)GetProcAddress(s_hmod_ws2_32, #f);\
if(NULL == km_##f)\
{\
FreeLibrary(s_hmod_ws2_32);\
s_hmod_ws2_32 = NULL;\
break;\
}
        
        s_hmod_ws2_32 = LoadLibrary("Ws2_32.dll");
        do
        {
            if(NULL == s_hmod_ws2_32)
                break;
            GET_WS2_FUNCTION(getaddrinfo);
            GET_WS2_FUNCTION(getnameinfo);
            GET_WS2_FUNCTION(freeaddrinfo);
            s_ipv6_support = true;
        } while (0);
    }
    
    return s_ipv6_support;
#else
    return true;
#endif
}

int km_resolve_2_ip_v4(const char* host_name, char *ip_buf, int ip_buf_len)
{
    const char* ptr = host_name;
    bool is_digit = true;
    while(*ptr)
    {
        if(*ptr != ' ' &&  *ptr != '\t')
        {
            if(*ptr != '.' && !(*ptr >= '0' && *ptr <= '9'))
            {
                is_digit = false;
                break;
            }
        }
        ++ptr;
    }
    
    if (is_digit)
    {
        STRNCPY_S(ip_buf, ip_buf_len, host_name, strlen(host_name));
        return 0;
    }
    
    struct hostent* he = NULL;
#ifdef KUMA_OS_LINUX
    int nError = 0;
    char szBuffer[1024] = {0};
    struct hostent *pheResultBuf = reinterpret_cast<struct hostent *>(szBuffer);
    
    if (::gethostbyname_r(
                          host_name,
                          pheResultBuf,
                          szBuffer + sizeof(struct hostent),
                          sizeof(szBuffer) - sizeof(struct hostent),
                          &pheResultBuf,
                          &nError) == 0)
    {
        he = pheResultBuf;
    }
#else
    he = gethostbyname(host_name);
#endif
    
    if(he && he->h_addr_list && he->h_addr_list[0])
    {
#ifndef KUMA_OS_WIN
        char tmp[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, he->h_addr_list[0], tmp, sizeof(tmp));
#else
        char* tmp = (char*)inet_ntoa((in_addr&)(*he->h_addr_list[0]));
#endif
        if(tmp)
        {
            STRNCPY_S(ip_buf, ip_buf_len, tmp, strlen(tmp));
            return 0;
        }
    }
    
    ip_buf[0] = 0;
    return -1;
}

extern "C" int km_resolve_2_ip(const char* host_name, char *ip_buf, int ip_buf_len, int ipv)
{
    if(NULL == host_name || NULL == ip_buf)
    {
        return -1;
    }
    
    ip_buf[0] = '\0';
#ifdef KUMA_OS_WIN
    if(!ipv6_api_init())
    {
        if(KM_RESOLVE_IPV6 == ipv)
            return -1;
        return km_resolve_2_ip_v4(host_name, ip_buf, ip_buf_len);
    }
#endif
    
    struct addrinfo* ai = NULL;
    if(km_getaddrinfo(host_name, NULL, NULL, &ai) != 0 || NULL == ai)
    {
        return -1;
    }
    
    for (struct addrinfo *aii = ai; aii; aii = aii->ai_next)
    {
        if(AF_INET6 == aii->ai_family && (KM_RESOLVE_IPV6 == ipv || KM_RESOLVE_IPV0 == ipv))
        {
            sockaddr_in6 *sa6 = (sockaddr_in6*)aii->ai_addr;
            if(IN6_IS_ADDR_LINKLOCAL(&(sa6->sin6_addr)))
                continue;
            if(IN6_IS_ADDR_SITELOCAL(&(sa6->sin6_addr)))
                continue;
            if(km_getnameinfo(aii->ai_addr, aii->ai_addrlen, ip_buf, ip_buf_len, NULL, 0, NI_NUMERICHOST|NI_NUMERICSERV) != 0)
                continue;
            else
                break; // found a ipv6 address
        }
        else if(AF_INET == aii->ai_family && (KM_RESOLVE_IPV4 == ipv || KM_RESOLVE_IPV0 == ipv))
        {
            if(km_getnameinfo(aii->ai_addr, aii->ai_addrlen, ip_buf, ip_buf_len, NULL, 0, NI_NUMERICHOST|NI_NUMERICSERV) != 0)
                continue;
            else
                break; // found a ipv4 address
        }
    }
    if('\0' == ip_buf[0] && KM_RESOLVE_IPV0 == ipv &&
       km_getnameinfo(ai->ai_addr, ai->ai_addrlen, ip_buf, ip_buf_len, NULL, 0, NI_NUMERICHOST|NI_NUMERICSERV) != 0)
    {
        km_freeaddrinfo(ai);
        return -1;
    }
    km_freeaddrinfo(ai);
    return 0;
}

extern "C" int km_set_sock_addr(const char* addr, unsigned short port,
                                struct addrinfo* hints, struct sockaddr * sk_addr,
                                unsigned int sk_addr_len)
{
#ifdef KUMA_OS_WIN
    if(!ipv6_api_init())
    {
        struct sockaddr_in *sa = (struct sockaddr_in*)sk_addr;
        sa->sin_family = AF_INET;
        sa->sin_port = htons(port);
        if(NULL == addr || addr[0] == '\0')
        {
            sa->sin_addr.s_addr = INADDR_ANY;
        }
        else
        {
#ifndef KUMA_OS_WIN
            inet_pton(sa->sin_family, addr, &sa->sin_addr);
#else
            sa->sin_addr.s_addr = inet_addr(addr);
#endif
        }
        return 0;
    }
#endif
    char service[128] = {0};
    struct addrinfo* ai = NULL;
    if(NULL == addr && hints)
    {
        hints->ai_flags |= AI_PASSIVE;
    }
    SNPRINTF(service, sizeof(service)-1, "%d", port);
    if(km_getaddrinfo(addr, service, hints, &ai) != 0 || NULL == ai || ai->ai_addrlen > sk_addr_len)
    {
        if(ai) km_freeaddrinfo(ai);
        return -1;
    }
    if(sk_addr)
        memcpy(sk_addr, ai->ai_addr, ai->ai_addrlen);
    km_freeaddrinfo(ai);
    return 0;
}

extern "C" int km_get_sock_addr(struct sockaddr * sk_addr, unsigned int sk_addr_len,
                                char* addr, unsigned int addr_len, unsigned short* port)
{
#ifdef KUMA_OS_WIN
    if(!ipv6_api_init())
    {
        struct sockaddr_in *sa = (struct sockaddr_in*)sk_addr;
#ifndef KUMA_OS_WIN
        inet_ntop(sa->sin_family, &sa->sin_addr, addr, addr_len);
#else
        STRNCPY_S(addr, addr_len, inet_ntoa(sa->sin_addr), addr_len-1);
#endif
        if(port)
            *port = ntohs(sa->sin_port);
        return 0;
    }
#endif
    
    char service[16] = {0};
    if(km_getnameinfo(sk_addr, sk_addr_len, addr, addr_len, service, sizeof(service), NI_NUMERICHOST|NI_NUMERICSERV) != 0)
        return -1;
    if(port)
        *port = atoi(service);
    return 0;
}

extern "C" bool km_is_ipv6_address(const char* addr)
{
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST;
    if(km_set_sock_addr(addr, 0, &hints, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) != 0)
    {
        return false;
    }
    return AF_INET6==ss_addr.ss_family;
}

extern "C" bool km_is_ip_address(const char* addr)
{
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST;
    return km_set_sock_addr(addr, 0, &hints, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) == 0;
}

extern "C" int km_parse_transport_address(const char* transport_address,
                                          char* protocol_type, int max_protocol_len,
                                          char* host_ip, int  max_host_ip_len, unsigned short* port)
{
    if(NULL==transport_address || NULL==host_ip)
        return -1;
    
    const char* tmp1 = NULL;
    int tmp_len = 0;
    const char* tmp = strstr(transport_address, "://");
    if(tmp)
    {
        tmp_len = int(max_protocol_len>tmp-transport_address?
            tmp-transport_address:max_protocol_len-1);
        
        if(protocol_type)
        {
            memcpy(protocol_type, transport_address, tmp_len);
            protocol_type[tmp_len] = '\0';
        }
        
        tmp += 3;
    }
    else
    {
        if(protocol_type)
            protocol_type[0] = '\0';
        tmp = transport_address;
    }
    const char* end = strchr(tmp, '/');
    if(NULL == end)
        end = transport_address + strlen(transport_address);
    
    tmp1 = strchr(tmp, '[');
    if(tmp1)
    {// ipv6 address
        tmp = tmp1 + 1;
        tmp1 = strchr(tmp, ']');
        if(!tmp1)
            return -1;
        tmp_len = int(max_host_ip_len>tmp1-tmp?
            tmp1-tmp:max_host_ip_len-1);
        memcpy(host_ip, tmp, tmp_len);
        host_ip[tmp_len] = '\0';
        tmp = tmp1 + 1;
        tmp1 = strchr(tmp, ':');
        if(tmp1 && tmp1 <= end)
            tmp = tmp1 + 1;
        else
            tmp = NULL;
    }
    else
    {// ipv4 address
        tmp1 = strchr(tmp, ':');
        if(tmp1 && tmp1 <= end)
        {
            tmp_len = int(max_host_ip_len>tmp1-tmp?
                tmp1-tmp:max_host_ip_len-1);
            memcpy(host_ip, tmp, tmp_len);
            host_ip[tmp_len] = '\0';
            tmp = tmp1 + 1;
        }
        else
        {
            tmp_len = int(max_host_ip_len>end-tmp?
                end-tmp:max_host_ip_len-1);
            memcpy(host_ip, tmp, tmp_len);
            host_ip[tmp_len] = '\0';
            tmp = NULL;
        }
    }
    
    if(port)
    {
        if(tmp)
            *port = atoi(tmp);
        else
            *port = 0;
    }
    
    return 0;
}

int set_nonblocking(int fd) {
#ifdef KUMA_OS_WIN
    int mode = 1;
    ::ioctlsocket(fd, FIONBIO, (ULONG*)&mode);
#else
    int flag = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK | O_ASYNC);
#endif
    return 0;
}

int find_first_set(unsigned int b)
{
    if(0 == b) {
        return -1;
    }
    int n = 0;
    if (!(0xffff & b))
        n += 16;
    if (!((0xff << n) & b))
        n += 8;
    if (!((0xf << n) & b))
        n += 4;
    if (!((0x3 << n) & b))
        n += 2;
    if (!((0x1 << n) & b))
        n += 1;
    return n;
}

TICK_COUNT_TYPE get_tick_count_ms()
{
#ifdef KUMA_HAS_CXX0X
    std::chrono::high_resolution_clock::time_point _now = std::chrono::high_resolution_clock::now();
    std::chrono::milliseconds _now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(_now.time_since_epoch());
    return _now_ms.count();
#else
#if defined(KUMA_OS_WIN)
    return GetTickCount();
#else
    TICK_COUNT_TYPE ret;
    struct timeval time_val;
    
    gettimeofday(&time_val, NULL);
    ret = time_val.tv_sec * 1000 + time_val.tv_usec / 1000;
    return ret;
#endif
#endif
}

TICK_COUNT_TYPE calc_time_elapse_delta_ms(TICK_COUNT_TYPE now_tick, TICK_COUNT_TYPE& start_tick)
{
    if(now_tick - start_tick > (((TICK_COUNT_TYPE)-1)>>1)) {
        start_tick = now_tick;
        return 0;
    }
    return now_tick - start_tick;
}

KUMA_NS_END