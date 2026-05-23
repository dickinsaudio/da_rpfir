#!/usr/bin/awk -f
# RAM Usage Analysis Script for RP2350
# Parses linker map file to show memory utilization

/^\.data\s+0x/ {
    data_addr = strtonum($2);
    data_size = strtonum($3);
}

/^\.bss\s+0x/ {
    bss_addr = strtonum($2);
    bss_size = strtonum($3);
}

END {
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
