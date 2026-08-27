// 极简 MinIO(S3) 客户端: 用后端下发的 STS 临时凭证, 直接对 MinIO 做上传(PUT)与下载(GET)。
// 完全自实现 AWS SigV4 签名(Web Crypto), 不依赖任何 S3 SDK。

async function toBytes(data) {
  if (data instanceof ArrayBuffer) return data;
  if (ArrayBuffer.isView(data)) return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
  if (typeof Blob !== 'undefined' && data instanceof Blob) return await data.arrayBuffer();
  return new TextEncoder().encode(String(data));
}

async function sha256Hex(data) {
  const buf = await toBytes(data);
  const digest = await crypto.subtle.digest('SHA-256', buf);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

async function hmac(key, data) {
  const cryptoKey = await crypto.subtle.importKey(
    'raw', key, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']
  );
  const sig = await crypto.subtle.sign(
    'HMAC', cryptoKey, new TextEncoder().encode(data)
  );
  return new Uint8Array(sig);
}

async function hmacHex(key, data) {
  return [...await hmac(key, data)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

function amzDate() {
  const now = new Date();
  const d = (n, l = 2) => String(n).padStart(l, '0');
  const stamp = `${now.getUTCFullYear()}${d(now.getUTCMonth() + 1)}${d(now.getUTCDate())}`;
  const time = `${d(now.getUTCHours())}${d(now.getUTCMinutes())}${d(now.getUTCSeconds())}`;
  return { stamp, full: `${stamp}T${time}Z` };
}

// 保留 '/' 与安全的字符, 其余按 S3 key 规则 URL 编码
function encodeKey(key) {
  return key.split('/').map((seg) => encodeURIComponent(seg)).join('/');
}

// 计算 SigV4 Authorization 头
async function signV4({ method, host, uri, query, headersStr, signedHeaders, payloadHash, amzdate, dateStamp, region, service, accessKey, secretKey }) {
  const canonicalRequest = [method, uri, query, headersStr, signedHeaders, payloadHash].join('\n');
  const scope = `${dateStamp}/${region}/${service}/aws4_request`;
  const stringToSign = ['AWS4-HMAC-SHA256', amzdate, scope, await sha256Hex(canonicalRequest)].join('\n');

  const kDate = await hmac(new TextEncoder().encode('AWS4' + secretKey), dateStamp);
  const kRegion = await hmac(kDate, region);
  const kService = await hmac(kRegion, service);
  const kSigning = await hmac(kService, 'aws4_request');
  const signature = await hmacHex(kSigning, stringToSign);

  return `AWS4-HMAC-SHA256 Credential=${accessKey}/${scope}, SignedHeaders=${signedHeaders}, Signature=${signature}`;
}

async function s3Request({ method, sts, key, body, contentType }) {
  const { stamp, full } = amzDate();
  const payloadHash = body ? await sha256Hex(body) : await sha256Hex('');

  let headersStr;
  let signedHeaders;
  const tokenHeader = `x-amz-security-token:${sts.sessionToken}\n`;

  if (method === 'PUT' && body) {
    headersStr = `content-type:${contentType}\nhost:${sts.endpoint}\nx-amz-content-sha256:${payloadHash}\nx-amz-date:${full}\n${tokenHeader}`;
    signedHeaders = 'content-type;host;x-amz-content-sha256;x-amz-date;x-amz-security-token';
  } else {
    headersStr = `host:${sts.endpoint}\nx-amz-content-sha256:${payloadHash}\nx-amz-date:${full}\n${tokenHeader}`;
    signedHeaders = 'host;x-amz-content-sha256;x-amz-date;x-amz-security-token';
  }

  const auth = await signV4({
    method, host: sts.endpoint, uri: `/${sts.bucket}/${encodeKey(key)}`, query: '',
    headersStr, signedHeaders, payloadHash,
    amzdate: full, dateStamp: stamp, region: sts.region,
    service: 's3', accessKey: sts.accessKeyId, secretKey: sts.secretAccessKey,
  });

  const url = `http://${sts.endpoint}/${sts.bucket}/${encodeKey(key)}`;
  const headers = {
    'Host': sts.endpoint,
    'X-Amz-Content-Sha256': payloadHash,
    'X-Amz-Date': full,
    'X-Amz-Security-Token': sts.sessionToken,
    'Authorization': auth,
  };
  if (method === 'PUT' && body) headers['Content-Type'] = contentType;

  const init = { method, headers, credentials: 'omit' };
  if (method === 'PUT' && body) init.body = body;

  const resp = await fetch(url, init);
  if (!resp.ok) {
    const text = await resp.text();
    throw new Error(`S3 ${method} ${key} failed (${resp.status}): ${text}`);
  }
  return resp;
}

// 向后端申请"写凭证"：只允许 PUT 单个对象 <目标snow_id>/<文件hash>
// kind: 'user'（私聊，目标=接收者 snow_id）| 'group'（群聊，目标=群 snow_id）
async function requestUploadSts({ kind, target, hash, duration }) {
  const params = new URLSearchParams();
  params.set('kind', kind);
  params.set('target', String(target));
  params.set('hash', hash);
  if (duration) params.set('duration', String(duration));
  const resp = await fetch(`/api/files/upload/sts?${params}`, { method: 'POST', credentials: 'include' });
  const j = await resp.json();
  if (j.status !== 'success') throw new Error(j.error || j.msg || '上传凭证申请失败');
  return j.data; // { endpoint, bucket, region, objectKey, accessKeyId, secretAccessKey, sessionToken, expiration }
}

// ---- 读凭证缓存：长时有效，仅在过期或请求失败时重新申请 ----
// fetchStsPromise 用于单飞：并发调用共享同一个进行中的请求，避免重复申请
let fetchStsCache = null;     // { sts, expiresAtMs }
let fetchStsPromise = null;   // 进行中的申请 Promise

function cacheFetchSts(sts) {
  const expiresAtMs = Date.parse(sts.expiration);
  fetchStsCache = {
    sts,
    // 提前 60s 视为过期，避免边界竞态
    expiresAtMs: Number.isFinite(expiresAtMs) ? expiresAtMs - 60 * 1000 : Date.now() + 3600 * 1000,
  };
}

// 实际向后端申请读凭证（不缓存）
async function requestFetchStsRaw() {
  const resp = await fetch('/api/files/fetch/sts', { method: 'POST', credentials: 'include' });
  const j = await resp.json();
  if (j.status !== 'success') throw new Error(j.error || j.msg || '读凭证申请失败');
  cacheFetchSts(j.data);
  return j.data;
}

// 向后端申请"读凭证"（覆盖当前用户所有可读位置），带缓存 + 单飞
async function requestFetchSts({ force = false } = {}) {
  if (!force && fetchStsCache && fetchStsCache.expiresAtMs > Date.now()) {
    return fetchStsCache.sts;
  }
  if (!fetchStsPromise) {
    fetchStsPromise = requestFetchStsRaw().finally(() => { fetchStsPromise = null; });
  }
  return fetchStsPromise;
}

// 上传对象, 返回对象 key
async function uploadObject(sts, key, blob, contentType) {
  await s3Request({ method: 'PUT', sts, key, body: blob, contentType });
  return key;
}

// 读凭证失效/失败时强制刷新
function invalidateFetchSts() {
  fetchStsCache = null;
  fetchStsPromise = null;
}

// 下载对象为 Blob（失败时自动刷新读凭证并重试一次）
async function downloadObject(sts, key) {
  const resp = await s3Request({ method: 'GET', sts, key });
  const contentType = resp.headers.get('Content-Type') || 'application/octet-stream';
  const blob = await resp.blob();
  return { blob, contentType };
}

export { requestUploadSts, requestFetchSts, invalidateFetchSts, uploadObject, downloadObject };