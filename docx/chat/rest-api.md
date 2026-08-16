# REST API 统一响应契约

> 本文档定义 `chatter.cc` 中 8 个聊天相关 REST 接口的统一响应格式。
> 后端已完成 Controller / Service / DAO 分层重构，所有接口响应统一归一为以下结构，
> 前端需按此契约适配（原分散的 `status/messages/friends/error/message` 字段已收敛）。

## 统一响应格式

所有接口返回 JSON，形状固定为四个顶层字段：

```json
{
    "code": 200,
    "status": "success",
    "msg": "ok",
    "data": {}
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `code` | int | HTTP 状态码（200/400/403/404/500） |
| `status` | string | 由 `code` 自动推导：`2xx`→`"success"`、`4xx`→`"failed"`、`5xx`→`"error"` |
| `msg` | string | 人类可读描述。成功为 `"ok"` 或操作反馈；失败为错误原因 |
| `data` | object/null | 业务数据。无数据时为 `null`；有数据时为对象 |

**归纳一句话**：成功看 `code==200 && status=="success"`，业务数据都在 `data` 里；失败看 `msg` 拿原因。

### 示例

成功（带数据）：
```json
{ "code": 200, "status": "success", "msg": "ok", "data": { "messages": [], "count": 0 } }
```

成功（无数据）：
```json
{ "code": 200, "status": "success", "msg": "Friend removed", "data": null }
```

业务失败：
```json
{ "code": 400, "status": "failed", "msg": "BAD_REQUEST: Invalid 'friendId'", "data": null }
```

服务端错误：
```json
{ "code": 500, "status": "error", "msg": "Database query timeout", "data": null }
```

## 鉴权约定

所有接口均要求携带 JWT（HttpOnly Cookie，键名 `jwttoken`，由配置 `permission_system.key` 控制）。

- JWT 缺失或校验失败 → `403`，`msg = "FORBIDDEN: Invalid or missing JWT token"`。
- JWT payload 中 `user_id <= 0` → `403`，`msg = "FORBIDDEN: Invalid JWT payload"`。

## 内层 Message 结构

拉取消息接口的 `data.messages[]` 元素为内层业务消息，结构与 `JettyCat::chat::Message::dump()` 一致：

```json
{
    "date": 1723456789,
    "from": 17,
    "type": "TEXT",
    "content": "hello"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `date` | uint64 | Unix 时间戳（秒） |
| `from` | int | 发送者 user_id |
| `type` | string | 消息类型：TEXT / IMAGE / VOICE / VIDEO / FILE |
| `content` | string | 消息内容 |

---

## 接口明细

### 1. GET /chat/fetch_user_message — 拉取私聊消息

| 项目 | 内容 |
|------|------|
| 方法 | GET |
| 参数 | `senderId`(必填)、`receiverId`(可选)、`offset`(可选) |

参数规则（后端已实现，前端注意）：
- `senderId` 缺失或非法 → `400`。
- `offset` 为负或非法 → `400`（`msg` 含 `'offset' must be non-negative` 或 `Invalid parameter value`）。
- `receiverId` 未提供 → 拉取"对方发给我的"消息（receiver 固定为当前登录用户）。
- `receiverId` 已提供 → 仅允许拉"我自己发给对方"，此时 `senderId` 必须等于当前登录用户，否则 `403`（`msg = "FORBIDDEN: 'receiverId' is only allowed when senderId is yourself"`）。

成功响应：
```json
{
    "code": 200, "status": "success", "msg": "ok",
    "data": { "messages": [ { "date": 0, "from": 17, "type": "TEXT", "content": "hi" } ], "count": 1 }
}
```

失败：`400`（参数）、`403`（越权）、`500`（`Database query timeout` / `Database query failed`）。

### 2. GET /chat/fetch_group_message — 拉取群聊消息

| 项目 | 内容 |
|------|------|
| 方法 | GET |
| 参数 | `groupId`(必填)、`offset`(可选) |

参数规则：`groupId` 缺失或非法 → `400`；`offset` 为负或非法 → `400`。

成功响应（`data` 结构与接口 1 一致）：
```json
{ "code": 200, "status": "success", "msg": "ok", "data": { "messages": [ ... ], "count": 1 } }
```

### 3. GET /chat/friend_list — 获取好友列表

| 项目 | 内容 |
|------|------|
| 方法 | GET |
| 参数 | 无（身份取自 JWT） |

成功响应：
```json
{
    "code": 200, "status": "success", "msg": "ok",
    "data": {
        "friends": [
            { "friend_id": 20, "username": "alice", "nickname": "Alice", "avatar_url": "http://..." }
        ],
        "count": 1
    }
}
```

`friends[]` 元素字段：`friend_id`(int)、`username`(string)、`nickname`(string)、`avatar_url`(string)。

### 4. GET /chat/group_list — 获取群聊列表

| 项目 | 内容 |
|------|------|
| 方法 | GET |
| 参数 | 无（身份取自 JWT） |

成功响应：
```json
{
    "code": 200, "status": "success", "msg": "ok",
    "data": {
        "groups": [
            { "group_id": 1, "group_name": "技术群", "identity": "member", "join_time": 1723456789 }
        ],
        "count": 1
    }
}
```

`groups[]` 元素字段：`group_id`(int)、`group_name`(string)、`identity`(string，如 `member`)、`join_time`(uint64)。

### 5. POST /chat/add_friend — 添加好友

| 项目 | 内容 |
|------|------|
| 方法 | POST |
| Body | `{ "friendId": 20 }`（数字或字符串均可） |

失败分支：
| code | msg |
|------|-----|
| 400 | `BAD_REQUEST: Missing or invalid 'friendId'`（缺失） |
| 400 | `BAD_REQUEST: Invalid 'friendId'`（非法或 `<=0`） |
| 400 | `BAD_REQUEST: Cannot add yourself as friend`（添加自己） |
| 404 | `NOT_FOUND: friendId does not exist`（目标用户不存在） |
| 500 | `Database query timeout` / `Database query failed` |

成功响应（`data` 为 `null`）：
```json
{ "code": 200, "status": "success", "msg": "Friend added", "data": null }
```

### 6. POST /chat/remove_friend — 删除好友

| 项目 | 内容 |
|------|------|
| 方法 | POST |
| Body | `{ "friendId": 20 }` |

失败分支：
| code | msg |
|------|-----|
| 400 | `BAD_REQUEST: Missing or invalid 'friendId'` |
| 400 | `BAD_REQUEST: Invalid 'friendId'`（非法、`<=0` 或等于自己） |
| 500 | `Database query timeout` / `Database query failed` |

成功响应：
```json
{ "code": 200, "status": "success", "msg": "Friend removed", "data": null }
```

### 7. POST /chat/add_group — 加入群聊

| 项目 | 内容 |
|------|------|
| 方法 | POST |
| Body | `{ "groupId": 1 }` |

失败分支：
| code | msg |
|------|-----|
| 400 | `BAD_REQUEST: Missing or invalid 'groupId'` |
| 400 | `BAD_REQUEST: Invalid 'groupId'`（非法或 `<=0`） |
| 404 | `NOT_FOUND: groupId does not exist`（群聊不存在） |
| 500 | `Database query timeout` / `Database query failed` |

成功响应：
```json
{ "code": 200, "status": "success", "msg": "Group joined", "data": null }
```

### 8. POST /chat/remove_group — 退出群聊

| 项目 | 内容 |
|------|------|
| 方法 | POST |
| Body | `{ "groupId": 1 }` |

失败分支：
| code | msg |
|------|-----|
| 400 | `BAD_REQUEST: Missing or invalid 'groupId'` |
| 400 | `BAD_REQUEST: Invalid 'groupId'`（非法或 `<=0`） |
| 500 | `Database query timeout` / `Database query failed` |

成功响应：
```json
{ "code": 200, "status": "success", "msg": "Group left", "data": null }
```

---

## 错误码汇总

| code | status | 场景 |
|------|--------|------|
| 200 | success | 操作成功 |
| 400 | failed | 参数缺失 / 非法 / 越界 / 操作自身 |
| 403 | failed | JWT 缺失或无效 / payload 非法 / receiverId 越权 |
| 404 | failed | 目标好友或群聊不存在 |
| 500 | error | 数据库超时或查询失败 |

## 与旧格式的对照（迁移指引）

| 旧字段写法 | 新写法 |
|-----------|--------|
| 成功 `{ "status": "success", "messages": [...], "count": N }` | `{ "code":200, "status":"success", "msg":"ok", "data": { "messages":[...], "count":N } }` |
| 成功 `{ "status": "success", "friends"/"groups": [...] }` | 对应数据移入 `data.friends` / `data.groups` |
| 成功 `{ "status": "success", "message": "Friend added" }` | `{ "code":200, "status":"success", "msg":"Friend added", "data":null }` |
| 失败 `{ "status": "failed"/"error", "error": "..." }` | `{ "code":<http码>, "status":"failed"/"error", "msg":"...", "data":null }` |

核心迁移点：判断成功改用 `resp.code === 200`（或 `status === "success"`）；业务字段（messages/friends/groups）改从 `resp.data` 读取；错误信息字段名 `error`/`message` 统一改为 `msg`。
