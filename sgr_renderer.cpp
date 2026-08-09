#include "sgr_renderer.hpp"

#include <stdio.h>

#include "da_rpfir.hpp"

void sgr_renderer_init()
{
    const uint input_pins[] = {
        MOD_RATE0_PIN,
        MOD_RATE1_PIN,
        MOD_44_48_PIN,
        MOD_DSD_PCM_PIN,
    };

    for (uint pin : input_pins)
    {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_disable_pulls(pin);
    }

    const uint output_pins[] = {
        MOD_RESET_PIN,
        CLK_22M_EN_PIN,
        CLK_24M_EN_PIN,
    };

    for (uint pin : output_pins)
    {
        gpio_init(pin);
        gpio_put(pin, true);
        gpio_set_dir(pin, GPIO_OUT);
    }
}

const char *cgi_sgr_renderer(const char *name, const char *arg, int len, char *buf)
{
    if (!name || !arg || !buf || len <= 0) return "";

    snprintf(buf, len,
        "<title>SGR Renderer</title>"
        "<h2>SGR RENDERER</h2>"
        "<div class=\"livebox\">"
        "<div>RATE0: <strong id=\"rate0\">-</strong></div>"
        "<div>RATE1: <strong id=\"rate1\">-</strong></div>"
        "<div>44/48: <strong id=\"rate44_48\">-</strong></div>"
        "<div>DSD/PCM: <strong id=\"dsd_pcm\">-</strong></div>"
        "</div>"
        "<script>"
        "function updateSgrRenderer(){"
        "fetch('sgr_renderer_status')"
        ".then(response=>response.json())"
        ".then(data=>{"
        "document.getElementById('rate0').innerText=data.rate0?'HIGH':'LOW';"
        "document.getElementById('rate1').innerText=data.rate1?'HIGH':'LOW';"
        "document.getElementById('rate44_48').innerText=data.rate44_48?'HIGH':'LOW';"
        "document.getElementById('dsd_pcm').innerText=data.dsd_pcm?'HIGH':'LOW';"
        "setTimeout(updateSgrRenderer,250);"
        "})"
        ".catch(()=>setTimeout(updateSgrRenderer,2000));"
        "}"
        "updateSgrRenderer();"
        "</script>"
        "</body></html>");
    return buf;
}

const char *cgi_sgr_renderer_status(const char *name, const char *arg, int len, char *buf)
{
    if (!name || !arg || !buf || len <= 0) return "";

    snprintf(buf, len,
        "{\"rate0\":%d,\"rate1\":%d,\"rate44_48\":%d,\"dsd_pcm\":%d}",
        gpio_get(MOD_RATE0_PIN),
        gpio_get(MOD_RATE1_PIN),
        gpio_get(MOD_44_48_PIN),
        gpio_get(MOD_DSD_PCM_PIN));
    return buf;
}