/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @brief Lua Kafka Module @file */

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <librdkafka/rdkafka.h>

#include "lauxlib.h"
#include "lua.h"

#ifdef LUA_SANDBOX
#include <luasandbox.h>
#include <luasandbox/heka/sandbox.h>
#include <luasandbox_output.h>
#endif

static const char *mozsvc_kafka_consumer  = "mozsvc.kafka_consumer";
static const char *mozsvc_kafka_producer  = "mozsvc.kafka_producer";
static const char *mozsvc_kafka_table     = "kafka";

typedef struct kafka_producer {
  rd_kafka_t  *rk;
  void        *msg_opaque;
#ifdef LUA_SANDBOX
  const lsb_logger  *logger;
#endif
  int         failures;
} kafka_producer;


typedef struct kafka_consumer {
  rd_kafka_t                      *rk;
  rd_kafka_topic_partition_list_t *topics;
#ifdef LUA_SANDBOX
  const lsb_logger  *logger;
#endif
} kafka_consumer;


typedef struct kafka_topic {
  rd_kafka_topic_t *rkt;
} kafka_topic;


static kafka_producer* check_producer(lua_State *lua,
                                      int min_args,
                                      int max_args)
{
  kafka_producer *kp = luaL_checkudata(lua, 1, mozsvc_kafka_producer);
  int n = lua_gettop(lua);
  luaL_argcheck(lua, n >= min_args && n <= max_args, n,
                "incorrect number of arguments");
  return kp;
}


#ifdef LUA_SANDBOX
static void log_cb(const rd_kafka_t *rk,
                   int level,
                   const char *fac,
                   const char *buf)
{
  if (!rk) {return;}
  kafka_producer *kp = rd_kafka_opaque(rk);
  kp->logger->cb(kp->logger->context, rd_kafka_name(rk), level, "%s\t%s", fac,
                 buf);
}


static int stats_cb(rd_kafka_t *rk,
                    char *json,
                    size_t json_len,
                    void *opaque)
{
  if (!rk) {return 0;}
  (void)json_len;
  kafka_producer *kp = opaque;
  kp->logger->cb(kp->logger->context, rd_kafka_name(rk), 6, "%s", json);
  return 0;
}
#endif


static void dr_msg_cb(rd_kafka_t *rk,
                   const rd_kafka_message_t *rkmessage,
                   void *opaque)
{
  if (!rk) {return;}
  kafka_producer *kp = opaque;
  kp->msg_opaque = rkmessage->_private;
  if (rkmessage->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
    ++kp->failures;
#ifdef LUA_SANDBOX
    kp->logger->cb(kp->logger->context, rd_kafka_name(rk), 3,
                   "delivery error\t%d\t%s", rkmessage->err,
                   rd_kafka_err2str(rkmessage->err));
#endif
  }
}


static bool load_conf(lua_State *lua, rd_kafka_conf_t *conf, int idx)
{
  if (!conf) {
    lua_pushstring(lua, "rd_kafka_conf_new() failed");
    return false;
  }
  if (lua_isnil(lua, idx)) {
    return true;
  }

  char errstr[512];
  lua_pushnil(lua);
  while (lua_next(lua, idx) != 0) {
    int kt = lua_type(lua, -2);
    int vt = lua_type(lua, -1);
    switch (kt) {
    case LUA_TSTRING:
      switch (vt) {
      case LUA_TSTRING:
        {
          const char *key = lua_tostring(lua, -2);
          const char *value = lua_tostring(lua, -1);
          if (value) {
            rd_kafka_conf_res_t r;
            r = rd_kafka_conf_set(conf, key, value, errstr, sizeof errstr);
            if (r) {
              lua_pushfstring(lua, "Failed to set %s = %s : %s", key, value,
                              errstr);
              return false;
            }
          }
        }
        break;
      case LUA_TNUMBER:
        {
          const char *key = lua_tostring(lua, -2);
          int i = (int)lua_tointeger(lua, -1);
          char value[12];
          snprintf(value, sizeof value, "%d", i);
          rd_kafka_conf_res_t r;
          r = rd_kafka_conf_set(conf, key, value, errstr, sizeof errstr);
          if (r) {
            lua_pushfstring(lua, "Failed to set %s = %s : %s", key, value,
                            errstr);
            return false;
          }
        }
        break;
      case LUA_TBOOLEAN:
        {
          const char *key = lua_tostring(lua, -2);
          const char *value = "false";
          if (lua_toboolean(lua, -1)) {
            value = "true";
          }
          rd_kafka_conf_res_t r;
          r = rd_kafka_conf_set(conf, key, value, errstr, sizeof errstr);
          if (r) {
            lua_pushfstring(lua, "Failed to set %s = %s : %s", key, value,
                            errstr);
            return false;
          }
        }
        break;
      default:
        lua_pushfstring(lua, "invalid config value type: %s",
                        lua_typename(lua, vt));
        return false;
      }
      break;
    default:
      lua_pushfstring(lua, "invalid config key type: %s",
                      lua_typename(lua, kt));
      return false;
    }
    lua_pop(lua, 1);
  }
  return true;
}


static
bool load_topic_conf(lua_State *lua, rd_kafka_topic_conf_t *conf, int idx)
{
  if (!conf) {
    lua_pushstring(lua, "rd_kafka_topic_conf_new() failed");
    return false;
  }
  if (lua_isnil(lua, idx)) {
    return true;
  }

  char errstr[512];
  lua_pushnil(lua);
  while (lua_next(lua, idx) != 0) {
    int kt = lua_type(lua, -2);
    int vt = lua_type(lua, -1);
    switch (kt) {
    case LUA_TSTRING:
      switch (vt) {
      case LUA_TSTRING:
        {
          const char *key = lua_tostring(lua, -2);
          const char *value = lua_tostring(lua, -1);
          if (value) {
            rd_kafka_conf_res_t r;
            r = rd_kafka_topic_conf_set(conf, key, value, errstr,
                                        sizeof errstr);
            if (r) {
              lua_pushfstring(lua, "Failed to set %s = %s : %s", key, value,
                              errstr);
              return false;
            }
          }
        }
        break;
      case LUA_TNUMBER:
        {
          const char *key = lua_tostring(lua, -2);
          int i = (int)lua_tointeger(lua, -1);
          char value[12];
          snprintf(value, sizeof value, "%d", i);
          rd_kafka_conf_res_t r;
          r = rd_kafka_topic_conf_set(conf, key, value, errstr,
                                      sizeof errstr);
          if (r) {
            lua_pushfstring(lua, "Failed to set %s = %s : %s", key, value,
                            errstr);
            return false;
          }
        }
        break;
      case LUA_TBOOLEAN:
        {
          const char *key = lua_tostring(lua, -2);
          const char *value = "false";
          if (lua_toboolean(lua, -1)) {
            value = "true";
          }
          rd_kafka_conf_res_t r;
          r = rd_kafka_topic_conf_set(conf, key, value, errstr,
                                      sizeof errstr);
          if (r) {
            lua_pushfstring(lua, "Failed to set %s = %s : %s", key, value,
                            errstr);
            return false;
          }
        }
        break;
      default:
        lua_pushfstring(lua, "invalid config value type: %s",
                        lua_typename(lua, vt));
        return false;
      }
      break;
    default:
      lua_pushfstring(lua, "invalid config key type: %s",
                      lua_typename(lua, kt));
      return false;
    }
    lua_pop(lua, 1);
  }
  return true;
}


static int producer_new(lua_State *lua)
{
  int n = lua_gettop(lua);
  luaL_argcheck(lua, n >= 1 && n <= 2, n, "incorrect number of arguments");

  const char *brokerlist = luaL_checkstring(lua, 1);
  int t = lua_type(lua, 2);
  switch (t) {
  case LUA_TNONE:
    lua_pushnil(lua);
    break;
  case LUA_TNIL:
    break;
  default:
    luaL_checktype(lua, 2, LUA_TTABLE); // producer config
  }

  kafka_producer *kp = lua_newuserdata(lua, sizeof(kafka_producer));
  kp->rk          = NULL;
  kp->msg_opaque  = NULL;
  kp->failures    = 0;
  lua_pushlightuserdata(lua, kp); // setup a topic table for this producer
  lua_newtable(lua);
  lua_rawset(lua, LUA_ENVIRONINDEX);
  luaL_getmetatable(lua, mozsvc_kafka_producer);
  lua_setmetatable(lua, -2);

  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  if (!load_conf(lua, conf, 2)) {
    rd_kafka_conf_destroy(conf);
    return lua_error(lua);
  }
  rd_kafka_conf_set_opaque(conf, kp);
  rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

#ifdef LUA_SANDBOX
  lua_getfield(lua, LUA_REGISTRYINDEX, LSB_THIS_PTR);
  lsb_lua_sandbox *lsb = lua_touserdata(lua, -1);
  lua_pop(lua, 1); // remove this ptr
  if (!lsb) {
    return luaL_error(lua, "invalid " LSB_THIS_PTR);
  }
  kp->logger = lsb_get_logger(lsb);
  if (kp->logger->cb) {
    rd_kafka_conf_set_log_cb(conf, log_cb);
    rd_kafka_conf_set_stats_cb(conf, stats_cb);
  } else {
    rd_kafka_conf_set_log_cb(conf, NULL); // disable logging
    rd_kafka_conf_set_stats_cb(conf, NULL); // disable stats
  }
#else
  rd_kafka_conf_set_log_cb(conf, NULL); // disable logging
  rd_kafka_conf_set_stats_cb(conf, NULL); // disable stats
#endif

  char errstr[512];
  kp->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof errstr);
  if (!kp->rk) {
    rd_kafka_conf_destroy(conf); // the producer has not taken ownership
    return luaL_error(lua, "rd_kafka_new failed: %s", errstr);
  }

  if (rd_kafka_brokers_add(kp->rk, brokerlist) == 0) {
    return luaL_error(lua, "invalid broker list");
  }
  return 1;
}


static kafka_topic* get_topic(lua_State *lua, kafka_producer *kp,
                              const char *topic)
{
  kafka_topic *kt = NULL;
  lua_pushlightuserdata(lua, kp);
  lua_rawget(lua, LUA_ENVIRONINDEX);
  lua_getfield(lua, -1, topic);
  if (lua_type(lua, -1) == LUA_TLIGHTUSERDATA) {
    kt = lua_touserdata(lua, -1);;
  }
  lua_pop(lua, 2);
  return kt;
}


static int producer_create_topic(lua_State *lua)
{
  kafka_producer *kp = check_producer(lua, 2, 3);
  const char *topic = NULL;

  topic = luaL_checkstring(lua, 2);

  int t = lua_type(lua, 3);
  switch (t) {
  case LUA_TNONE:
    lua_pushnil(lua);
    break;
  case LUA_TNIL:
    break;
  default:
    luaL_checktype(lua, 3, LUA_TTABLE); // topic config
  }

  if (get_topic(lua, kp, topic)) {
    return 0;
  }

  kafka_topic *kt = malloc(sizeof(kafka_topic));
  if (!kt) {
    return luaL_error(lua, "memory allocation failed");
  }

  rd_kafka_topic_conf_t *tconf = rd_kafka_topic_conf_new();
  if (!load_topic_conf(lua, tconf, 3)) {
    rd_kafka_topic_conf_destroy(tconf);
    free(kt);
    return lua_error(lua);
  }

  char errstr[512];
  kt->rkt = rd_kafka_topic_new(kp->rk, topic, tconf);
  if (!kt->rkt) {
    rd_kafka_topic_conf_destroy(tconf);
    free(kt);
    return luaL_error(lua, "rd_kafka_topic_new failed: %s", errstr);
  }
  lua_pushlightuserdata(lua, kp);
  lua_rawget(lua, LUA_ENVIRONINDEX);
  lua_pushstring(lua, topic);
  lua_pushlightuserdata(lua, kt);
  lua_rawset(lua, -3); // update the producer topic table
  return 0;
}


static int producer_has_topic(lua_State *lua)
{
  int n = lua_gettop(lua);
  luaL_argcheck(lua, 2 == n, n, "incorrect number of arguments");
  kafka_producer *kp = luaL_checkudata(lua, 1, mozsvc_kafka_producer);
  const char *topic =  luaL_checkstring(lua, 2);
  if (get_topic(lua, kp, topic)) {
    lua_pushboolean(lua, true);
  } else {
    lua_pushboolean(lua, false);
  }
  return 1;
}


static int producer_destroy_topic(lua_State *lua)
{
  int n = lua_gettop(lua);
  luaL_argcheck(lua, 2 == n, n, "incorrect number of arguments");
  kafka_producer *kp = luaL_checkudata(lua, 1, mozsvc_kafka_producer);
  const char *topic =  luaL_checkstring(lua, 2);
  kafka_topic *kt = get_topic(lua, kp, topic);
  if (kt) {
    lua_pushlightuserdata(lua, kp);
    lua_rawget(lua, LUA_ENVIRONINDEX);
    lua_pushnil(lua);
    lua_setfield(lua, -2, topic);
    rd_kafka_topic_destroy(kt->rkt);
    free(kt);
  }
  return 0;
}


#ifdef LUA_SANDBOX
static int producer_poll_heka(lua_State *lua)
{
  kafka_producer *kp = check_producer(lua, 1, 1);
  kp->failures = 0;
  kp->msg_opaque = NULL;
  rd_kafka_poll(kp->rk, 0);
  if (kp->msg_opaque) {
    lua_getfield(lua, LUA_GLOBALSINDEX, LSB_HEKA_UPDATE_CHECKPOINT);
    if (lua_type(lua, -1) == LUA_TFUNCTION) {
      lua_pushlightuserdata(lua, kp->msg_opaque);
      lua_pushinteger(lua, kp->failures);
      if (lua_pcall(lua, 2, 0, 0)) {
        lua_error(lua);
      }
    } else {
      luaL_error(lua, LSB_HEKA_UPDATE_CHECKPOINT " was not found");
    }
    lua_pop(lua, 1);
  }
  return 0;
}


static int producer_send_heka(lua_State *lua)
{
  static const int msg_idx = 5;
  kafka_producer *kp = check_producer(lua, msg_idx, msg_idx);

  const char *topic = luaL_checkstring(lua, 2);
  kafka_topic *kt = get_topic(lua, kp, topic);
  if (!kt) return luaL_error(lua, "invalid topic");

  int32_t partition = (int32_t)luaL_checkinteger(lua, 3);
  luaL_checktype(lua, 4, LUA_TLIGHTUSERDATA);
  void *sequence_id = lua_touserdata(lua, 4);

  int msgflags = RD_KAFKA_MSG_F_COPY;
  size_t len = 0;
  const char *msg = NULL;

  switch (lua_type(lua, msg_idx)) {
  case LUA_TSTRING:
    msg = lua_tolstring(lua, msg_idx, &len);
    break;
  case LUA_TUSERDATA:
    {
      lua_CFunction fp = lsb_get_zero_copy_function(lua, msg_idx);
      if (!fp) {
        return luaL_argerror(lua, msg_idx, "no zero copy support");
      }
      int results = fp(lua);
      int start = msg_idx + 1;
      int end = start + results;
      int segments = 0;
      size_t total_len = 0;

      for (int i = start; i < end; ++i) {
        switch (lua_type(lua, i)) {
        case LUA_TSTRING:
          msg = lua_tolstring(lua, i, &len);
          break;
        case LUA_TLIGHTUSERDATA:
          msg = lua_touserdata(lua, i++);
          len = (size_t)lua_tointeger(lua, i);
          break;
        default:
          return luaL_error(lua, "invalid zero copy return");
        }
        total_len += len;
        ++segments;
      }

      if (segments == 0 || total_len == 0) {
        lua_pushinteger(lua, 0);
        return 1;
      }

      if (segments > 1) {
        char *buf = malloc(total_len);
        if (!buf) {
          return luaL_error(lua, "malloc failed");
        }

        size_t pos = 0;
        for (int i = start; i < end; ++i) {
          switch (lua_type(lua, i)) {
          case LUA_TSTRING:
            msg = lua_tolstring(lua, i, &len);
            break;
          case LUA_TLIGHTUSERDATA:
            msg = lua_touserdata(lua, i++);
            len = (size_t)lua_tointeger(lua, i);
            break;
          }
          if (msg && len > 0) {
            memcpy(buf + pos, msg, len);
            pos += len;
          }
        }
        msg = buf;
        len = total_len;
        msgflags = RD_KAFKA_MSG_F_FREE; // give ownership to kafka
      }
    }
    break;

  default:
    return luaL_typerror(lua, msg_idx, "string or userdata");
    break;
  }

  errno = 0;
  int ret = rd_kafka_produce(kt->rkt, partition,
                             msgflags,
                             (void *)msg, len,
                             NULL, 0, // optional key/len
                             sequence_id // opaque pointer
                            );
  if (ret == -1) {
    lua_pushinteger(lua, errno);
  } else {
    lua_pushinteger(lua, 0);
  }
  return 1;
}
#endif


static int producer_poll(lua_State *lua)
{
  kafka_producer *kp = check_producer(lua, 1, 2);
  int timeout = luaL_optint(lua, 2, 0);
  kp->failures = 0;
  kp->msg_opaque = NULL;
  rd_kafka_poll(kp->rk, timeout);
  if (kp->msg_opaque) {
    uintptr_t sequence_id = (uintptr_t)kp->msg_opaque;
    lua_pushnumber(lua, (lua_Number)sequence_id);
  } else {
    lua_pushnil(lua);
  }
  lua_pushinteger(lua, kp->failures);
  return 2;
}


static int producer_send(lua_State *lua)
{
  kafka_producer *kp = check_producer(lua, 5, 5);

  const char *topic = luaL_checkstring(lua, 2);
  kafka_topic *kt = get_topic(lua, kp, topic);
  if (!kt) return luaL_error(lua, "invalid topic");

  int32_t partition = (int32_t)luaL_checkinteger(lua, 3);

  lua_Number sid = lua_tonumber(lua, 4);
  if (sid < 0 || sid > UINTPTR_MAX) {
    return luaL_error(lua, "sequence_id out of range");
  }
  uintptr_t sequence_id = (uintptr_t)sid;

  size_t len = 0;
  const char *msg = luaL_checklstring(lua, 5, &len);


  errno = 0;
  int ret = rd_kafka_produce(kt->rkt, partition,
                             RD_KAFKA_MSG_F_COPY,
                             (void *)msg, len,
                             NULL, 0, // optional key/len
                             (void *)sequence_id // opaque pointer
                            );
  if (ret == -1) {
    lua_pushinteger(lua, errno);
  } else {
    lua_pushinteger(lua, 0);
  }
  return 1;
}


static int producer_gc(lua_State *lua)
{
  kafka_producer *kp = check_producer(lua, 1, 1);
  lua_pushlightuserdata(lua, kp);
  lua_rawget(lua, LUA_ENVIRONINDEX);
  assert(lua_type(lua, -1) == LUA_TTABLE);
  lua_pushnil(lua);  /* first key */
  while (lua_next(lua, -2) != 0) {
    kafka_topic *kt = lua_touserdata(lua, -1);
    if (kt) {
      rd_kafka_topic_destroy(kt->rkt);
      free(kt);
    }
    lua_pop(lua, 1);
  }
  if (kp->rk) rd_kafka_destroy(kp->rk);

  lua_pushlightuserdata(lua, kp);
  lua_pushnil(lua);
  lua_rawset(lua, LUA_ENVIRONINDEX); // remove the producer topic table

  // This may timeout because it might not be the last sandbox running.
  rd_kafka_wait_destroyed(1000);
  return 0;
}


static kafka_consumer* check_consumer(lua_State *lua, int args)
{
  kafka_consumer *kc = luaL_checkudata(lua, 1, mozsvc_kafka_consumer);
  int n = lua_gettop(lua);
  luaL_argcheck(lua, args == n, n, "incorrect number of arguments");
  return kc;
}


static bool add_consumer_topics(lua_State *lua,
                                kafka_consumer *kc,
                                int cnt)
{
  bool is_subscription = true;

  kc->topics = rd_kafka_topic_partition_list_new(cnt);
  if (!kc->topics) {
    lua_pushstring(lua, "rd_kafka_topic_partition_list_new failed");
    return false;
  }

  lua_pushnil(lua);
  while (lua_next(lua, 2) != 0) {
    int kt = lua_type(lua, -2);
    int vt = lua_type(lua, -1);
    if (kt != LUA_TNUMBER || vt != LUA_TSTRING) {
      lua_pushstring(lua, "topics must be an array of strings");
      return false;
    }
    const char *topic = lua_tostring(lua, -1);
    char *t;
    long partition = -1;

    if ((t = strstr(topic, ":"))) { // Parse "topic[:partition]
      char s[strlen(topic)];
      memcpy(s, topic, t - topic);
      s[t - topic] = 0;

      partition = strtol(t + 1, NULL, 10);
      if (partition > INT32_MAX) {
        lua_pushstring(lua, "invalid topic partition > INT32_MAX");
        return false;
      } else if (partition < 0) {
        lua_pushstring(lua, "invalid topic partition < 0");
        return false;
      }
      is_subscription = false;
      rd_kafka_topic_partition_list_add(kc->topics, s, (int32_t)partition);
    } else {
      rd_kafka_topic_partition_list_add(kc->topics, topic, (int32_t)partition);
    }
    lua_pop(lua, 1);
  }

  rd_kafka_resp_err_t err;
  if (is_subscription) {
    if ((err = rd_kafka_subscribe(kc->rk, kc->topics))) {
      lua_pushfstring(lua, "rd_kafka_subscribe failed: %s",
                      rd_kafka_err2str(err));
      return false;
    }

  } else {
    if ((err = rd_kafka_assign(kc->rk, kc->topics))) {
      lua_pushfstring(lua, "rd_kafka_assign failed: %s", rd_kafka_err2str(err));
      return false;
    }
  }
  return true;
}


static int consumer_new(lua_State *lua)
{
  static const char *group_id = "group.id";
  int n = lua_gettop(lua);
  luaL_argcheck(lua, n >= 3 && n <= 4, n, "incorrect number of arguments");

  const char *brokerlist = luaL_checkstring(lua, 1);

  luaL_checktype(lua, 2, LUA_TTABLE); // topics
  int topic_cnt = (int)lua_objlen(lua, 2);
  luaL_argcheck(lua, topic_cnt > 0, 2, "the topics array is empty");

  luaL_checktype(lua, 3, LUA_TTABLE); // consumer config

  int t = lua_type(lua, 4); // topic config
  switch (t) {
  case LUA_TNONE:
    lua_pushnil(lua);
    break;
  case LUA_TNIL:
    break;
  default:
    luaL_checktype(lua, 3, LUA_TTABLE);
  }

  kafka_consumer *kc = lua_newuserdata(lua, sizeof(kafka_consumer));
  kc->rk = NULL;
  kc->topics = NULL;
  luaL_getmetatable(lua, mozsvc_kafka_consumer);
  lua_setmetatable(lua, -2);

  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  if (!load_conf(lua, conf, 3)) {
    rd_kafka_conf_destroy(conf);
    return lua_error(lua);
  }

#ifdef LUA_SANDBOX
  rd_kafka_conf_set_opaque(conf, kc);
  lua_getfield(lua, LUA_REGISTRYINDEX, LSB_THIS_PTR);
  lsb_lua_sandbox *lsb = lua_touserdata(lua, -1);
  lua_pop(lua, 1); // remove this ptr
  if (!lsb) {
    return luaL_error(lua, "invalid " LSB_THIS_PTR);
  }
  kc->logger = lsb_get_logger(lsb);
  if (kc->logger->cb) {
    rd_kafka_conf_set_log_cb(conf, log_cb);
    rd_kafka_conf_set_stats_cb(conf, stats_cb);
  } else {
    rd_kafka_conf_set_log_cb(conf, NULL); // disable logging
    rd_kafka_conf_set_stats_cb(conf, NULL); // disable stats
  }
#else
  rd_kafka_conf_set_log_cb(conf, NULL); // disable logging
  rd_kafka_conf_set_stats_cb(conf, NULL); // disable stats
#endif

  char errstr[512];
  size_t len;
  if (rd_kafka_conf_get(conf, group_id, NULL, &len) != RD_KAFKA_CONF_OK) {
    rd_kafka_conf_destroy(conf);
    return luaL_error(lua, "%s must be set", group_id);
  }

  rd_kafka_topic_conf_t *tconf = rd_kafka_topic_conf_new();
  if (!load_topic_conf(lua, tconf, 4)) {
    rd_kafka_topic_conf_destroy(tconf);
    rd_kafka_conf_destroy(conf);
    return lua_error(lua);
  }
  if (rd_kafka_topic_conf_set(tconf, "offset.store.method", "broker",
                              errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK
      || rd_kafka_topic_conf_set(tconf, "auto.commit.enable", "true",
                                 errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
    rd_kafka_topic_conf_destroy(tconf);
    rd_kafka_conf_destroy(conf);
    return luaL_error(lua, "rd_kafka_topic_conf_set failed: %s", errstr);
  }
  rd_kafka_conf_set_default_topic_conf(conf, tconf);

  kc->rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof errstr);
  if (!kc->rk) {
    rd_kafka_conf_destroy(conf);
    return luaL_error(lua, "rd_kafka_new failed: %s", errstr);
  }

  if (rd_kafka_brokers_add(kc->rk, brokerlist) == 0) {
    return luaL_error(lua, "invalid broker list");
  }

  rd_kafka_poll_set_consumer(kc->rk);
  if (!add_consumer_topics(lua, kc, topic_cnt)) {
    return lua_error(lua);
  }
  return 1;
}


static int consumer_receive(lua_State *lua)
{
  bool err = false;
  kafka_consumer *kc = check_consumer(lua, 1);
  rd_kafka_message_t *rkmessage = rd_kafka_consumer_poll(kc->rk, 1000);
  if (rkmessage) {
    if (rkmessage->err) {
      if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
          rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
        if (rkmessage->rkt) {
          err = true;
          lua_pushfstring(lua, "topic: %s partition: %d offset: %g err: %s",
                          rd_kafka_topic_name(rkmessage->rkt),
                          (int)rkmessage->partition,
                          (double)rkmessage->offset,
                          rd_kafka_message_errstr(rkmessage));
        } else {
          err = true;
          lua_pushfstring(lua, "%s err: %s", rd_kafka_err2str(rkmessage->err),
                          rd_kafka_message_errstr(rkmessage));
        }
      } else {
        lua_pushnil(lua);
        lua_pushnil(lua);
        lua_pushnil(lua);
        lua_pushnil(lua);
      }
    } else {
      lua_pushlstring(lua, rkmessage->payload, rkmessage->len);
      lua_pushstring(lua, rd_kafka_topic_name(rkmessage->rkt));
      lua_pushinteger(lua, (lua_Integer)rkmessage->partition);
      if (rkmessage->key_len) {
        lua_pushlstring(lua, rkmessage->key, rkmessage->key_len);
      } else {
        lua_pushnil(lua);
      }
    }
    rd_kafka_message_destroy(rkmessage);
  } else {
    lua_pushnil(lua);
    lua_pushnil(lua);
    lua_pushnil(lua);
    lua_pushnil(lua);
  }
  if (err) return lua_error(lua);
  return 4;
}


static int consumer_gc(lua_State *lua)
{
  kafka_consumer *kc = check_consumer(lua, 1);
  if (kc->rk) rd_kafka_consumer_close(kc->rk);
  if (kc->topics) rd_kafka_topic_partition_list_destroy(kc->topics);
  if (kc->rk) rd_kafka_destroy(kc->rk);
  rd_kafka_wait_destroyed(1000);
  return 0;
}


static int kafka_version(lua_State *lua)
{
  lua_pushstring(lua, DIST_VERSION);
  return 1;
}


static const struct luaL_reg kafkalib_f[] = {
  { "consumer", consumer_new },
  { "producer", producer_new },
  { "version", kafka_version },
  { NULL, NULL }
};


static const struct luaL_reg producerlib_m[] = {
  { "create_topic", producer_create_topic },
  { "has_topic", producer_has_topic },
  { "destroy_topic", producer_destroy_topic },
  { "poll", producer_poll },
  { "send", producer_send },
  { "__gc", producer_gc },
  { NULL, NULL }
};

#ifdef LUA_SANDBOX
static const struct luaL_reg producerlibext_m[] = {
  { "poll", producer_poll_heka },
  { "send", producer_send_heka },
  { NULL, NULL }
};
#endif


static const struct luaL_reg consumerlib_m[] = {
  { "receive", consumer_receive },
  { "__gc", consumer_gc },
  { NULL, NULL }
};


int luaopen_kafka(lua_State *lua)
{
  luaL_newmetatable(lua, mozsvc_kafka_producer);
  lua_pushvalue(lua, -1);
  lua_setfield(lua, -2, "__index");
  luaL_register(lua, NULL, producerlib_m);

#ifdef LUA_SANDBOX
  lua_getfield(lua, LUA_REGISTRYINDEX, LSB_HEKA_THIS_PTR);
  lsb_heka_sandbox *hsb = lua_touserdata(lua, -1);
  lua_pop(lua, 1); // remove this ptr
  if (hsb) {
    luaL_register(lua, NULL, producerlibext_m);
  }
#endif
  lua_pop(lua, 1);

  luaL_newmetatable(lua, mozsvc_kafka_consumer);
  lua_pushvalue(lua, -1);
  lua_setfield(lua, -2, "__index");
  luaL_register(lua, NULL, consumerlib_m);
  lua_pop(lua, 1);

  luaL_register(lua, mozsvc_kafka_table, kafkalib_f);
  return 1;
}
