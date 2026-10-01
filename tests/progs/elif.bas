for i = 0 to 5
  if i = 0
    ? "zero"
  else
    if i = 1
      ? "one"
    else
      if i = 2
        ? "two"
      else
        ? "many"
      endif
    endif
  endif
  if i > 3
    ? "big"
  else
    if i = 3 then ? "three"
  endif
next i
' Comments must be kept
a = 1
if a = 0
  ? "zero"
else ' not zero
  if a = 1
    ? "one"
  endif
endif
if a = 0
  ? "zero"
else
  if a = 1 ' one?
    ? "one"
  endif ' end inner
endif
if a = 0
  ? "zero"
else
  if a = 1
    ? "one"
  endif ' end inner
endif ' end outer
