# chatter 接口定义

## websocket 通信格式定义

``` json 
{
    "code" : 200,
    "from" : "username(system)",
    "to" : "username(system)",
    "message" : "hello world",
    "reason" : "ok"
}
```

## HTTP REST 接口

### 用户历史消息拉取

**接口**: `GET /chat/fetch_user_message`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**说明**: 拉取"**某位发送者发给我的**"私聊消息,即 `sender_id` 由前端传入,`receiver_id` 由 JWT 中的 `userid` 决定,不接受前端传入接收者 id。

**请求参数** (URL Query Params):

```
GET /chat/fetch_user_message?senderId=17&offset=0
```

| 参数名 | 类型 | 必填 | 含义 |
|--------|------|------|------|
| `senderId` | int | 是 | 消息发送者 id(聊天对方) |
| `offset` | int | 否 | 分页偏移量, 默认 0。每页返回条数由配置文件 `chatter.single_fetch_count` 控制 |

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "messages": [
        {
            "date": 1234567890,
            "from": 17,
            "type": "TEXT",
            "content": "hello"
        }
    ],
    "count": 1
}
```

| 字段名 | 类型 | 含义 |
|--------|------|------|
| `status` | string | "success" / "error" / "failed" |
| `messages` | array | 消息列表, 按 `send_time` 降序排列 |
| `messages[].date` | int | 发送时间的 UNIX 时间戳 (秒) |
| `messages[].from` | int | 发送者用户 id (即 `senderId`, 真正发消息的人) |
| `messages[].type` | string | 消息类型: `TEXT`, `IMAGE`, `VOICE`, `VIDEO`, `FILE` |
| `messages[].content` | string | 消息内容 |
| `count` | int | 本页返回的消息条数 |

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (400 — 参数错误):
``` json
{
    "status": "failed",
    "error": "BAD_REQUEST: Missing or invalid 'senderId'"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query timeout"
}
```

---

### 群组历史消息拉取

**接口**: `GET /chat/fetch_group_message`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**请求参数** (URL Query Params):

```
GET /chat/fetch_group_message?groupId=20&offset=0
```

| 参数名 | 类型 | 必填 | 含义 |
|--------|------|------|------|
| `groupId` | int | 是 | 目标群组 id |
| `offset` | int | 否 | 分页偏移量, 默认 0。每页返回条数由配置文件 `chatter.single_fetch_count` 控制 |

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "messages": [
        {
            "date": 1234567890,
            "from": 17,
            "type": "TEXT",
            "content": "hello group"
        }
    ],
    "count": 1
}
```

| 字段名 | 类型 | 含义 |
|--------|------|------|
| `status` | string | "success" / "error" / "failed" |
| `messages` | array | 消息列表, 按 `send_time` 降序排列 |
| `messages[].date` | int | 发送时间的 UNIX 时间戳 (秒) |
| `messages[].from` | int | 发送者用户 id |
| `messages[].type` | string | 消息类型: `TEXT`, `IMAGE`, `VOICE`, `VIDEO`, `FILE` |
| `messages[].content` | string | 消息内容 |
| `count` | int | 本页返回的消息条数 |

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (400 — 参数错误):
``` json
{
    "status": "failed",
    "error": "BAD_REQUEST: Missing or invalid 'groupId'"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query failed"
}
```

    ---

### 好友列表获取

**接口**: `GET /chat/friend_list`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**说明**: 获取当前登录用户的好友列表,用户身份完全由 JWT 中的 `userid` 决定,不接受前端传入用户 id。仅返回 `status = 1`(正常)的好友。

**请求参数**: 无 (URL Query Params 为空)

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "friends": [
        {
            "friend_id": 20,
            "username": "koddnty",
            "nickname": "koddnty",
            "avatar_url": "https://example.com/avatar.png"
        }
    ],
    "count": 1
}
```

| 字段名 | 类型 | 含义 |
|--------|------|------|
| `status` | string | "success" / "error" / "failed" |
| `friends` | array | 好友列表 |
| `friends[].friend_id` | int | 好友用户 id |
| `friends[].username` | string | 好友用户名 |
| `friends[].nickname` | string | 好友昵称 (可为空) |
| `friends[].avatar_url` | string | 好友头像 URL (可为空) |
| `count` | int | 好友数量 |

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query failed"
}
```

> **说明**: `user_friend` 表通过 CHECK 约束保证 `user_id < friend_id`,因此查询好友时需双向匹配:好友既可能作为 `friend_id`(当自己是较小 id)也可能作为 `user_id`(当自己是较大 id)。后端使用 `UNION` 查询覆盖两个方向。

---

### 群聊列表获取

**接口**: `GET /chat/group_list`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**说明**: 获取当前登录用户加入的所有群聊,用户身份完全由 JWT 中的 `userid` 决定,不接受前端传入用户 id。

**请求参数**: 无 (URL Query Params 为空)

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "groups": [
        {
            "group_id": 1,
            "group_name": "测试群1",
            "identity": "master",
            "join_time": 1785849433
        }
    ],
    "count": 1
}
```

| 字段名 | 类型 | 含义 |
|--------|------|------|
| `status` | string | "success" / "error" / "failed" |
| `groups` | array | 群聊列表 |
| `groups[].group_id` | int | 群聊 id |
| `groups[].group_name` | string | 群聊名称 |
| `groups[].identity` | string | 用户在群内的身份: `master`(群主) / `manager`(管理员) / `member`(成员) |
| `groups[].join_time` | int | 加入群聊的 UNIX 时间戳 (秒) |
| `count` | int | 群聊数量 |

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query failed"
}
```

---

### 添加好友

**接口**: `GET /chat/add_friend`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**说明**: 将指定用户添加为当前用户的好友。用户身份由 JWT 中的 `userid` 决定。`user_friend` 表通过 CHECK 约束保证 `user_id < friend_id`,后端会自动对两个 id 排序后写入。若关系已存在(如之前删除过),会自动恢复为正常状态(`status = 1`)。

**请求参数** (URL Query Params):

```
GET /chat/add_friend?friendId=20
```

| 参数名 | 类型 | 必填 | 含义 |
|--------|------|------|------|
| `friendId` | int | 是 | 要添加的好友用户 id |

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "message": "Friend added"
}
```

失败 (400 — 参数错误):
``` json
{
    "status": "failed",
    "error": "BAD_REQUEST: Missing or invalid 'friendId'"
}
```

失败 (400 — 不能添加自己):
``` json
{
    "status": "failed",
    "error": "BAD_REQUEST: Cannot add yourself as friend"
}
```

失败 (404 — 目标用户不存在):
``` json
{
    "status": "failed",
    "error": "NOT_FOUND: friendId does not exist"
}
```

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query failed"
}
```

---

### 删除好友

**接口**: `GET /chat/remove_friend`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**说明**: 删除当前用户与指定用户的好友关系。用户身份由 JWT 中的 `userid` 决定。**删除仅做标记**: 将 `user_friend.status` 置为 `0`(拉黑/删除),不物理删除记录。

**请求参数** (URL Query Params):

```
GET /chat/remove_friend?friendId=20
```

| 参数名 | 类型 | 必填 | 含义 |
|--------|------|------|------|
| `friendId` | int | 是 | 要删除的好友用户 id |

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "message": "Friend removed"
}
```

失败 (400 — 参数错误):
``` json
{
    "status": "failed",
    "error": "BAD_REQUEST: Missing or invalid 'friendId'"
}
```

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query failed"
}
```

---

### 加入群聊

**接口**: `GET /chat/add_group`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**说明**: 将当前用户加入指定群聊,身份默认为 `member`。用户身份由 JWT 中的 `userid` 决定。若已加入,刷新 `join_time`。

**请求参数** (URL Query Params):

```
GET /chat/add_group?groupId=1
```

| 参数名 | 类型 | 必填 | 含义 |
|--------|------|------|------|
| `groupId` | int | 是 | 要加入的群聊 id |

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "message": "Group joined"
}
```

失败 (400 — 参数错误):
``` json
{
    "status": "failed",
    "error": "BAD_REQUEST: Missing or invalid 'groupId'"
}
```

失败 (404 — 目标群聊不存在):
``` json
{
    "status": "failed",
    "error": "NOT_FOUND: groupId does not exist"
}
```

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query failed"
}
```

---

### 退出群聊

**接口**: `GET /chat/remove_group`

**认证**: Cookie 中携带 `jwttoken` (HttpOnly, SameSite=Strict)

**说明**: 将当前用户从指定群聊移除。用户身份由 JWT 中的 `userid` 决定。**注意**: `user_group` 表当前没有 `status` 字段,无法做标记删除,此接口暂以物理删除(`DELETE`)实现。

**请求参数** (URL Query Params):

```
GET /chat/remove_group?groupId=1
```

| 参数名 | 类型 | 必填 | 含义 |
|--------|------|------|------|
| `groupId` | int | 是 | 要退出的群聊 id |

**响应体** (JSON):

成功 (200):
``` json
{
    "status": "success",
    "message": "Group left"
}
```

失败 (400 — 参数错误):
``` json
{
    "status": "failed",
    "error": "BAD_REQUEST: Missing or invalid 'groupId'"
}
```

失败 (403 — JWT 缺失或无效):
``` json
{
    "status": "failed",
    "error": "FORBIDDEN: Invalid or missing JWT token"
}
```

失败 (500 — 数据库错误):
``` json
{
    "status": "error",
    "error": "Database query failed"
}
```
