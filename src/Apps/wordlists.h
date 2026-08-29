// wordlists.h — small bundled dictionaries for the OSINT + leak apps.
//
// These are compiled in so the apps have a usable default list with zero
// filesystem setup on a fresh Pi. The lists are intentionally compact
// (few hundred entries each) to keep code size small; operators can
// override with a larger file in hal_storage.
//
//   * SUBDOMAINS  — common hostnames used by app_osint SUBDOM brute.
//   * CREDENTIALS — common weak passwords used by app_leak HASH crack.

#pragma once

// ── Subdomain brute list (app_osint SUBDOM) ──────────────────────────
static const char *const OSINT_SUBDOMAINS[] = {
    "www", "mail", "smtp", "pop", "pop3", "imap", "ns1", "ns2", "ns3",
    "mx", "mx1", "mx2", "ftp", "sftp", "ssh", "web", "webmail", "admin",
    "administrator", "cpanel", "whm", "root", "api", "app", "dev",
    "development", "staging", "stage", "test", "testing", "qa", "uat",
    "beta", "alpha", "demo", "portal", "intranet", "extranet", "vpn",
    "remote", "git", "svn", "jenkins", "ci", "cd", "build", "docs",
    "wiki", "support", "help", "status", "statuspage", "cdn", "static",
    "assets", "media", "img", "images", "files", "download", "uploads",
    "mobile", "m", "shop", "store", "cart", "shopify", "blog", "news",
    "forum", "community", "login", "signin", "auth", "sso", "oauth",
    "secure", "security", "gateway", "backup", "db", "mysql", "sql",
    "redis", "cache", "maria", "mongo", "postgres", "old", "new", "www2",
    "m2", "v2", "v3", "preview", "live", "production", "dev2", "tst",
    "lab", "sandbox", "playground", "monitor", "stats", "analytics",
    "grafana", "kibana", "elastic", "prometheus", "jira", "confluence",
    "harvest", "mxrelay", "relay", "sip", "voip", "pbx", "asterisk",
    "phishing", "owa", "autodiscover", "lync", "skype", "teams", "mail2",
};
#define OSINT_SUBDOM_COUNT (sizeof(OSINT_SUBDOMAINS) / sizeof(OSINT_SUBDOMAINS[0]))

// ── Credential / weak-password list (app_leak HASH) ──────────────────
static const char *const LEAK_CREDENTIALS[] = {
    "password", "password1", "password123", "Password1", "P@ssw0rd",
    "123456", "1234567", "12345678", "123456789", "1234567890",
    "12345", "1234", "qwerty", "qwerty123", "abc123", "admin", "admin123",
    "root", "toor", "letmein", "welcome", "welcome1", "monkey", "dragon",
    "iloveyou", "sunshine", "princess", "football", "baseball", "shadow",
    "master", "login", "test", "test123", "guest", "changeme", "default",
    "1q2w3e4r", "qazwsx", "zxcvbn", "trustno1", "passw0rd", "p@ssw0rd",
    "Whatever1", "Winter2020", "Summer2020", "Spring2021", "company",
    "letmein1", "hunter", "hunter2", "cheese", "orange", "pepper", "secret",
    "tigger", "snoopy", "freedom", "whatever", "batman", "superman",
    "asdfgh", "asdf", "zxcvbnm", "1qaz2wsx", "!@#$%^&*", "startrek",
};
#define LEAK_CRED_COUNT (sizeof(LEAK_CREDENTIALS) / sizeof(LEAK_CREDENTIALS[0]))