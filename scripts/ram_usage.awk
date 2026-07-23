#!/usr/bin/awk -f
# RAM Usage Analysis Script for RP2350
# Parses linker map file to show memory utilization

/^\.data[[:space:]]+0x/ {
    addr = parse_num($2);
    size = parse_num($3);

    # Keep the final linked .data section in SRAM, not per-object placeholders.
    if (addr >= 0x20000000 && size > 0) {
        data_addr = addr;
        data_size = size;
    }
}

/^\.bss[[:space:]]+0x/ {
    addr = parse_num($2);
    size = parse_num($3);

    # Keep the final linked .bss section in SRAM, not per-object placeholders.
    if (addr >= 0x20000000 && size > 0) {
        bss_addr = addr;
        bss_size = size;
    }
}

function parse_num(s,    i, c, v, n) {
    # Portable parser: supports both decimal and 0x-prefixed hex values.
    if (s ~ /^0[xX][0-9a-fA-F]+$/) {
        s = substr(s, 3);
        n = 0;
        for (i = 1; i <= length(s); i++) {
            c = substr(s, i, 1);
            if (c >= "0" && c <= "9") {
                v = c + 0;
            } else {
                c = tolower(c);
                v = index("abcdef", c);
                if (v == 0) return 0;
                v += 9;
            }
            n = (n * 16) + v;
        }
        return n;
    }

    return s + 0;
}

END {
    if (data_size == "") data_size = 0;
    if (bss_size == "") bss_size = 0;

    total = data_size + bss_size;
    rp2350_ram = 520 * 1024;
    used_pct = (total / rp2350_ram) * 100;
    over = total - rp2350_ram;
    
    printf ".data section:     %7d bytes (%6.1f KB)\n", data_size, data_size/1024.0;
    printf ".bss section:      %7d bytes (%6.1f KB)\n", bss_size, bss_size/1024.0;
    printf "                   -------        ------\n";
    printf "TOTAL RAM USED:    %7d bytes (%6.1f KB)\n\n", total, total/1024.0;
    printf "RP2350 Available:  %7d bytes (%6.1f KB)\n\n", rp2350_ram, rp2350_ram/1024.0;
    
    if (over > 0) {
        printf "⚠️  EXCEEDED BY:     %7d bytes (%6.1f KB) ⚠️\n", over, over/1024.0;
        printf "RAM usage: %.1f%% of available\n\n", used_pct;
        printf "STATUS: WILL NOT FIT IN RAM - CRITICAL ISSUE!\n";
        exit 1;  # Fail the build if over memory limit
    } else {
        remaining = -over;
        printf "✓  Remaining:      %7d bytes (%6.1f KB)\n", remaining, remaining/1024.0;
        printf "RAM usage: %.1f%% of available\n\n", used_pct;
        if (remaining < 50*1024) {
            printf "STATUS: TIGHT - Less than 50KB free (risk of stack overflow)\n";
        } else if (remaining < 100*1024) {
            printf "STATUS: OK - Consider reducing buffers for more headroom\n";
        } else {
            printf "STATUS: OK - Adequate headroom\n";
        }
    }
}
