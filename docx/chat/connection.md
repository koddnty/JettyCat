# connection WebSocket 连接模块

## 概述

`code/chat/connection.cc` 负责 WebSocket 聊天连接的完整生命周期管理,包括:

- 连接建立时的身份验证与会话注册
- 消息的接收、解析与路由分发
- 私聊消息的持久化与转发
- 心跳保活与 Redis 会话过期刷新
- 连接关闭/异常时的会话清理

## 端点注册

WebSocket 服务路径为 `/chat`,通过 `ChatWebSocketServer::registeUrl` 注册:

```cpp
void ChatWebSocketServer::registeUrl(const m_sylar::websocket::WsServer::ptr server) {
    server->registerUrl<ChatHandler>("/chat");
}
```

连接地址: `ws://<host>:8803/chat`

## 连接生命周期

### 1. 连接建立 (`co_onOpen`)

WebSocket 握手成功后触发,流程:

1. **JWT 身份验证** — 从握手请求的 Cookie 中读取 `jwttoken`(键名由配置 `permission_system.key` 控制,默认 `jwttoken`),调用 `JWT::verifyJWT` 验证。
   - 验证失败:发送 `401` 错误帧,并以 `1008 Unauthorized` 关闭连接。
2. **查询用户 id** — 根据 JWT payload 中的 `username` 查询 MySQL `users` 表,获得 `user_id`。
3. **Redis 会话注册** — 执行 `SADD {instanceId}_user_{userId} {sessionId}`,建立 用户 id → sessionId 的集合映射。Redis 键以 `getInstanceId()`(服务器启动时间戳)为前缀,防止跨重启冲突。
4. **设置会话过期** — 执行 `EXPIRE {instanceId}_user_{userId} 36000`(10 小时)。失败则回滚删除映射并以 `1011 Internal Server Error` 关闭。
5. **绑定会话用户信息** — 创建 `UserChatInfo`(含 `user_name`、`user_id`、`role`、`pongLoop`),通过 `session->setData(user)` 挂载到会话。
6. **发送欢迎消息** — 向客户端推送 `200` 欢迎帧:

``` json
{
    "code": 200,
    "type": "",
    "reason": "ok",
    "content": "wellcome, {username}!",
    "from": 0,
    "to": 17
}
```

### 2. 消息接收 (`co_onMessage`)

收到文本消息后:

1. 反序列化外层 `WsMessage` JSON。解析失败 → 返回 `400 Failed to parse message` 错误帧。
2. 根据 `WsMessage.type` 通过 `WsMessageRouter` 路由分发。
3. 无匹配 handler → 返回 `404 Unknown message type: {type}` 错误帧。

### 3. 消息路由 (`WsMessageRouter`)

`chat/connection.cc` 中通过 `ChatWebSocketServer::initWsRoutes()` 注册路由:

```cpp
router.on("private_message", handlePrivateMessage);
```

路由匹配优先级: `(type, from, to)` 精确匹配 > `(type, 0, 0)` 通配匹配。

## 消息处理

### 私聊消息 (`private_message`)

**客户端发送格式**:

``` json
{
    "code": 200,
    "type": "private_message",
    "from": 17,
    "to": 20,
    "reason": "ok",
    "content": "{\"date\":0,\"from\":17,\"type\":\"TEXT\",\"content\":\"hello\"}"
}
```

`content` 字段为内层业务 `Message` 的 JSON 序列化。

**处理流程** (`handlePrivateMessage`):

1. **发送者校验** — `ws_msg.getFrom()` 必须等于会话绑定的 `user_id`,否则返回 `400 Sender mismatch`。
2. **解析业务消息** — 从 `content` 反序列化 `Message`;失败 → `400 Failed to parse message content`。随后强制 `setFrom(user_id)` 覆盖为会话身份,防止伪造发送者。
3. **数据库持久化** — 调用 `sendToUser(receiver_id, msg_list)` 写入 `user_message` 表(`sender_id`, `receiver_id`, `content`, `extra`)。失败 → `500 Failed to persist message`。
4. **Redis 查找目标** — `SMEMBERS {instanceId}_user_{receiver_id}` 获取目标用户的在线 sessionId 列表。
   - Redis 查询失败 → `500 Internal server error`。
   - 列表为空(目标离线)→ 消息已持久化,直接返回(离线消息待上线后拉取)。
5. **转发** — 构造 `private_message` 转发帧,遍历目标 sessionId 逐个发送:

``` json
{
    "code": 200,
    "type": "private_message",
    "from": 17,
    "to": 20,
    "reason": "ok",
    "content": "{\"date\":1234567890,\"from\":17,\"type\":\"TEXT\",\"content\":\"hello\"}"
}
```

**接收方收到的帧格式**: `code=200`, `type=private_message`, `from`=发送者 id, `to`=接收者 id, `content`=内层业务消息 JSON。

### 二进制消息 (`co_onBinary`)

二进制帧不受支持,收到后返回 `400 unsupported binary message` 错误帧。

## 心跳保活 (`co_onPong`)

客户端发送 WebSocket Ping,服务器收到 Pong 后触发 `co_onPong`:

- `UserChatInfo.pongLoop` 计数,每累计 1000 次 Pong 刷新一次 Redis 过期时间:`EXPIRE {instanceId}_user_{userId} 36000`(减少 Redis 通信次数)。
- 刷新失败仅记录日志,不关闭连接。

## 连接关闭

### 正常关闭 (`co_onClose`)

1. 从会话数据中取出 `UserChatInfo`。
2. 执行 `SREM {instanceId}_user_{userId} {sessionId}` 删除会话映射。
3. 删除失败记录错误日志。

### 异常关闭 (`co_onBadClose` / `co_onError`)

连接因错误或异常终止时触发,同样执行 `SREM` 清理用户-会话映射。

## Redis 会话键说明

| Redis 命令 | 作用 |
|------------|------|
| `SADD {instanceId}_user_{userId} {sessionId}` | 注册会话(用户可多端登录,一个用户多个 session) |
| `SMEMBERS {instanceId}_user_{userId}` | 查找用户所有在线会话 |
| `SREM {instanceId}_user_{userId} {sessionId}` | 移除会话(连接关闭) |
| `EXPIRE {instanceId}_user_{userId} 36000` | 设置/刷新会话过期时间 |

`{instanceId}` = `getInstanceId()`(服务器启动 epoch 时间戳),`{sessionId}` 由 sylar 框架为每个 WsSession 分配。

## 会话用户信息 (`UserChatInfo`)

继承自 `websocket::SessionInfoBase`,通过 `session->setData()` 挂载:

| 字段 | 类型 | 含义 |
|------|------|------|
| `user_name` | `std::string` | 用户名(JWT payload) |
| `user_id` | `int` | 用户 id(数据库查询所得) |
| `role` | `RolePermissions::Role` | 角色(USER / ADMIN) |
| `pongLoop` | `int` | Pong 计数,用于控制 Redis 刷新频率 |

## 错误码汇总

| HTTP 状态码 | 场景 |
|-------------|------|
| 200 | 成功 |
| 400 | 发送者不匹配 / 消息解析失败 / 不支持的二进制消息 |
| 401 | JWT 验证失败 |
| 404 | 未知消息类型 |
| 500 | 数据库持久化失败 / Redis 查询失败 |

| WebSocket 关闭码 | 场景 |
|------------------|------|
| 1008 | 策略违规(JWT 未通过) |
| 1011 | 服务器内部错误(Redis 过期设置失败 / 会话数据绑定失败) |
