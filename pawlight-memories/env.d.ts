declare namespace Cloudflare {
  interface Env {
    DB: D1Database;
    FILES: R2Bucket;
    LOCAL_SINGLE_USER_ID?: string;
    GATEWAY_BASE_URL?: string;
    CONTROL_TOKEN?: string;
  }
}
