// SPDX-License-Identifier: BSD-2-Clause-Patent
// No-op definitions of the probe hooks the patched XNU references while instantnoodle_defconfig leaves their component out.
// The no-display hook also starts the opt-in board hardware threads.

// offline-component-load-probes.patch, bsd_init: called on offline-legacy-register=1 (NativeLegacyDisplay).
void native_legacy_display_probe(void);
void instantnoodle_platform_init(void);
void native_legacy_display_probe(void)
{
    instantnoodle_platform_init();
}
