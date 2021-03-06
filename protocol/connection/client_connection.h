/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#pragma once

#include "config.h"

#include <cJSON.h>
#include <cJSON_utils.h>
#include <cstdlib>
#include <engines/ewouldblock_engine/ewouldblock_engine.h>
#include <libgreenstack/Greenstack.h>
#include <memcached/openssl.h>
#include <memcached/protocol_binary.h>
#include <memcached/types.h>
#include <platform/dynamic.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <utilities/protocol2text.h>

enum class Protocol : uint8_t {
    Memcached,
    Greenstack
};

/**
 * The Frame class is used to represent all of the data included in the
 * protocol unit going over the wire. For the memcached binary protocol
 * this is either the full request or response as defined in
 * memcached/protocol_binary.h, and for greenstack this is the greenstack
 * frame as defined in libreenstack/Frame.h
 */
class Frame {
public:
    void reset() {
        payload.resize(0);
    }

    std::vector<uint8_t> payload;
    typedef std::vector<uint8_t>::size_type size_type;
};

class DocumentInfo {
public:
    std::string id;
    uint32_t flags;
    std::string expiration;
    Greenstack::Compression compression;
    Greenstack::Datatype datatype;
    uint64_t cas;
};

class Document {
public:
    DocumentInfo info;
    std::vector<uint8_t> value;
};

class MutationInfo {
public:
    uint64_t cas;
    size_t size;
    uint64_t seqno;
    uint64_t vbucketuuid;
};

class ConnectionError : public std::runtime_error {
public:
    explicit ConnectionError(const char* what_arg)
        : std::runtime_error(what_arg) {
        // Empty
    }

    virtual uint16_t getReason() const = 0;

    virtual Protocol getProtocol() const = 0;

    virtual bool isInvalidArguments() const = 0;

    virtual bool isAlreadyExists() const = 0;

    virtual bool isNotMyVbucket() const = 0;

    virtual bool isNotFound() const = 0;

    virtual bool isNotStored() const = 0;

    virtual bool isAccessDenied() const = 0;
};

/**
 * The MemcachedConnection class is an abstract class representing a
 * connection to memcached. The concrete implementations of the class
 * implements the Memcached binary protocol and Greenstack.
 *
 * By default a connection is set into a synchronous mode.
 *
 * All methods is expeted to work, and all failures is reported through
 * exceptions. Unexpected packets / responses etc will use the ConnectionError,
 * and other problems (like network error etc) use std::runtime_error.
 *
 */
class MemcachedConnection {
public:
    MemcachedConnection() = delete;

    MemcachedConnection(const MemcachedConnection&) = delete;

    virtual ~MemcachedConnection();

    // Creates clone (copy) of the given connection - i.e. a second independent
    // channel to memcached. Used for multi-connection testing.
    virtual std::unique_ptr<MemcachedConnection> clone() = 0;

    in_port_t getPort() const {
        return port;
    }

    sa_family_t getFamily() const {
        return family;
    }

    bool isSsl() const {
        return ssl;
    }

    const Protocol& getProtocol() const {
        return protocol;
    }

    bool isSynchronous() const {
        return synchronous;
    }

    virtual void setSynchronous(bool enable) {
        if (!enable) {
            std::runtime_error("MemcachedConnection::setSynchronous: Not implemented");
        }
    }

    /**
     * Perform a SASL authentication to memcached
     *
     * @param username the username to use in authentication
     * @param password the password to use in authentication
     * @param mech the SASL mech to use
     */
    virtual void authenticate(const std::string& username,
                              const std::string& password,
                              const std::string& mech) = 0;

    /**
     * Create a bucket
     *
     * @param name the name of the bucket
     * @param config the buckets configuration attributes
     * @param type the kind of bucket to create
     */
    virtual void createBucket(const std::string& name,
                              const std::string& config,
                              const Greenstack::BucketType& type) = 0;

    /**
     * Delete the named bucket
     *
     * @param name the name of the bucket
     */
    virtual void deleteBucket(const std::string& name) = 0;

    /**
     * Select the named bucket
     *
     * @param name the name of the bucket to select
     */
    virtual void selectBucket(const std::string& name) = 0;

    /**
     * List all of the buckets on the server
     *
     * @return a vector containing all of the buckets
     */
    virtual std::vector<std::string> listBuckets() = 0;

    /**
     * Fetch a document from the server
     *
     * @param id the name of the document
     * @param vbucket the vbucket the document resides in
     * @return a document object containg the information about the
     *         document.
     */
    virtual Document get(const std::string& id, uint16_t vbucket) = 0;

    /*
     * Form a Frame representing a CMD_GET
     */
    virtual Frame encodeCmdGet(const std::string& id, uint16_t vbucket) = 0;

    /*
     * Form a Frame representing a CMD_DCP_OPEN
     */
    virtual Frame encodeCmdDcpOpen() = 0;

    /*
     * Form a Frame representing a CMD_DCP_STREAM_REQ
     */
    virtual Frame encodeCmdDcpStreamReq() = 0;

    /**
     * Perform the mutation on the attached document.
     *
     * The method throws an exception upon errors
     *
     * @param doc the document to mutate
     * @param vbucket the vbucket to operate on
     * @param type the type of mutation to perform
     * @return the new cas value for success
     */
    virtual MutationInfo mutate(const Document& doc, uint16_t vbucket,
                                const Greenstack::mutation_type_t type) = 0;


    virtual unique_cJSON_ptr stats(const std::string& subcommand) = 0;

    /**
     * Instruct the audit daemon to reload the configuration
     */
    virtual void reloadAuditConfiguration() = 0;

    /**
     * Sent the given frame over this connection
     *
     * @param frame the frame to send to the server
     */
    virtual void sendFrame(const Frame& frame);

    /** Send part of the given frame over this connection. Upon success,
     * the frame's payload will be modified such that the sent bytes are
     * deleted - i.e. after a successful call the frame object will only have
     * the remaining, unsent bytes left.
     *
     * @param frame The frame to partially send.
     * @param length The number of bytes to transmit. Must be less than or
     *               equal to the size of the frame.
     */
    void sendPartialFrame(Frame& frame, Frame::size_type length);

    /**
     * Receive the next frame on the connection
     *
     * @param frame the frame object to populate with the next frame
     */
    virtual void recvFrame(Frame& frame) = 0;

    /**
     * Get a textual representation of this connection
     *
     * @return a textual representation of the connection including the
     *         protocol and any special attributes
     */
    virtual std::string to_string() = 0;

    void reconnect();

    /**
     * Try to configure the ewouldblock engine
     *
     * See the header /engines/ewouldblock_engine/ewouldblock_engine.h
     * for a full description on the parameters.
     */
    virtual void configureEwouldBlockEngine(const EWBEngineMode& mode,
                                            ENGINE_ERROR_CODE err_code = ENGINE_EWOULDBLOCK,
                                            uint32_t value = 0,
                                            const std::string& key = "") = 0;


    /**
     * Identify ourself to the server and fetch the available SASL mechanisms
     * (available by calling `getSaslMechanisms()`
     *
     * @throws std::runtime_error if an error occurs
     */
    virtual void hello(const std::string& userAgent,
                       const std::string& userAgentVersion,
                       const std::string& comment) = 0;


    /**
     * Get the servers SASL mechanisms. This is only valid after running a
     * successful `hello()`
     */
    const std::string& getSaslMechanisms() const {
        return saslMechanisms;
    }

    /**
     * Request the IOCTL value from the server
     *
     * @param key the IOCTL to request
     * @return A textual representation of the key
     */
    virtual std::string ioctl_get(const std::string& key) {
        throw std::invalid_argument("Not implemented");
    }

    /**
     * Perform an IOCTL on the server
     *
     * @param key the IOCTL to set
     * @param value the value to specify for the given key
     */
    virtual void ioctl_set(const std::string& key,
                           const std::string& value) {
        throw std::invalid_argument("Not implemented");
    }

protected:
    /**
     * Create a new instance of the MemcachedConnection
     *
     * @param host the hostname to connect to (empty == localhost)
     * @param port the port number to connect to
     * @param family the socket family to connect as (AF_INET, AF_INET6
     *               or use AF_UNSPEC to just pick one)
     * @param ssl connect over SSL or not
     * @param protocol the protocol the implementation is using
     * @return
     */
    MemcachedConnection(const std::string& host, in_port_t port,
                        sa_family_t family, bool ssl,
                        const Protocol& protocol);

    void close();

    void connect();

    void read(Frame& frame, size_t bytes);

    void readPlain(Frame& frame, size_t bytes);

    void readSsl(Frame& frame, size_t bytes);

    void sendFramePlain(const Frame& frame);

    void sendFrameSsl(const Frame& frame);

    std::string host;
    in_port_t port;
    sa_family_t family;
    bool ssl;
    Protocol protocol;
    SSL_CTX* context;
    BIO* bio;
    SOCKET sock;
    bool synchronous;
    std::string saslMechanisms;
};

class ConnectionMap {
public:
    /**
     * Initialize the connection map with connections matching the ports
     * opened from Memcached
     */
    void initialize(cJSON* ports);

    /**
     * Invalidate all of the connections
     */
    void invalidate();

    /**
     * Get a connection object matching the given attributes
     *
     * @param protocol The requested protocol (Greenstack / Memcached)
     * @param ssl If ssl should be enabled or not
     * @param family the network family (IPv4 / IPv6)
     * @param port (optional) The specific port number to use..
     * @return A connection object to use
     * @throws std::runtime_error if the request can't be served
     */
    MemcachedConnection& getConnection(const Protocol& protocol,
                                       bool ssl,
                                       sa_family_t family = AF_INET,
                                       in_port_t port = 0);

    /**
     * Do we have a connection matching the requested attributes
     */
    bool contains(const Protocol& protocol, bool ssl, sa_family_t family);

private:
    std::vector<MemcachedConnection*> connections;
};
