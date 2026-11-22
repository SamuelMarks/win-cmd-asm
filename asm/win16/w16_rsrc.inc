;; @file w16_rsrc.inc
;; @isa 8086
;; @desc Embedded Win16 NE Resource Table & payloads (RT_ICON, RT_GROUP_ICON, RT_MENU) for Windows 1.0..3.2.

@cpu 8086
@format inc

; --- NE Resource Table (rscAlignShift = 4 -> 16-byte sectors) ---
; Contains 3 resource types:
;   1. RT_ICON       (0x8003), ID 0x8001 -> file offset 0x0200 (sector 0x0020, len 0x0009 = 144B)
;   2. RT_GROUP_ICON (0x800E), ID 0x8001 -> file offset 0x0290 (sector 0x0029, len 0x0002 = 32B)
;   3. RT_MENU       (0x8004), ID 0x8001 -> file offset 0x02B0 (sector 0x002B, len 0x0004 = 64B)
@data dw rsrcAlignShift, 0x0004

; Type 1: RT_ICON (0x8003), count = 1
@data dw rsrcTypeIcon, 0x8003, 0x0001, 0x0000, 0x0000
@data dw rsrcIconItem, 0x0020, 0x0009, 0x1C30, 0x8001, 0x0000, 0x0000

; Type 2: RT_GROUP_ICON (0x800E), count = 1
@data dw rsrcTypeGrpIcon, 0x800E, 0x0001, 0x0000, 0x0000
@data dw rsrcGrpIconItem, 0x0029, 0x0002, 0x1C30, 0x8001, 0x0000, 0x0000

; Type 3: RT_MENU (0x8004), count = 1
@data dw rsrcTypeMenu, 0x8004, 0x0001, 0x0000, 0x0000
@data dw rsrcMenuItem, 0x002B, 0x0004, 0x1C30, 0x8001, 0x0000, 0x0000

; End of NE Resource Types (rscEndTypes = 0x0000) + empty resource name list (0x00)
@data dw rsrcEndTypes, 0x0000
@data db rsrcEndNames, 0x00, 0x00
