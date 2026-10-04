' DATA names share one-letter names with variables, except where FastBasic
' would read them as the variable.
DATA bytes() BYTE = 1, 2, 3
DATA words() = 1000, 2000
DATA floats%() = 1.5, 2.25
DATA changed() BYTE = 0, 0
count = 3
total = 0
ratio% = 0.5
text$ = "LABELS"

for i = 0 to count - 1
  total = total + bytes(i)
next i

' Addresses of integer and floating point DATA
? &bytes, adr(words), &floats%
' Floating point DATA, read next to floating point variables
ratio% = ratio% + floats%(1)
' Assigned to, also after THEN
words(1) = total
if total > 5 then changed(1) = total
get words(0)
input "?"; words(1)
? total, ratio%, text$, changed(1), words(1)
