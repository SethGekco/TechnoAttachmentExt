Stolen from:
https://github.com/Phobos-developers/Phobos/pull/352
It is made stand-alone with intentions of making it easier to debug possible serious issues while also making it easy to update your Phobos without rebuilding your own dll. 

Almost all of the ground work is in great debt to Kerbiter. See his Phobos PR for all "supported" (lul, not fully tested, something prob broked) tags. 

; [attachtype]
; Prerequisite=GAPILL                ; (existing) all must be present
; Prerequisite.Negative=GATECH       ; NEW: blocked while ANY listed building is present
; RequiredHouses=Americans,British   ; NEW: host owner's country must be one of these
; ForbiddenHouses=Russians           ; NEW: host owner's country must NOT be listed
; Prerequisite.Dynamic=yes           ; if prerequisites are not met and the unit or building is constructed, it wont get the attachments when they are. If the prerequisites are met, they wont be lost when the prerequisites vanish/gets destroyed
;
; [parent]
; Attachment0.Type=TestAttach
; Attachment0.TechnoType=HTNK
; Attachment0.FLH=100,0,0
; Attachment0.Prerequisite.Negative=NAWEAP
; Attachment0.RequiredHouses=Germans
; Attachment0.ForbiddenHouses=Americans
; Attachment0.Prerequisite.Dynamic=no

All parent overrides AttachmentType
