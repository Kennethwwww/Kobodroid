/* Kobo experiment only. Bionic property initialization returns:
 * 0 success, -1 allocation/open/mapping failure, -2 label-write failure
 * after successfully initializing every property mapping. Only 0/-2 return.
 * Used only with external private-file errno95 + inherited-kernel-context gate.
 */
.syntax unified
.thumb
.section .text
.global property_init_compat
.thumb_func
property_init_compat:
 push {r7, lr}
 bl original_property_area_init
 cbz r0, .Lok
 cmn r0, #2
 beq .Lok
 movs r0, #1
 bl original_exit
.Lok:
 pop {r7, pc}
 .balign 2
