#pragma once

void sgr_renderer_init();

const char *cgi_sgr_renderer(const char *name, const char *arg, int len, char *buf);
const char *cgi_sgr_renderer_status(const char *name, const char *arg, int len, char *buf);