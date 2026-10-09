{
    global:
        Plugify_*;

        /* s2sdk binding slots (defined by s2sdk_bindings, filled by the plugify module) */
        __s2sdk_*;

        /* levels_ranks binding slots (filled by the plugify module from the levels_ranks plugin) */
        __levels_ranks_*;

        /* vip binding slots (filled by the plugify module from the vip plugin) */
        __vip_*;

        /* hudmenu binding slots (defined by hudmenu_bindings, PUBLIC-linked through pisexcompat) */
        __hudmenu_*;

        /* No exported methods: lr_vip_multi exposes nothing to other plugins. */

    local: *;
};
