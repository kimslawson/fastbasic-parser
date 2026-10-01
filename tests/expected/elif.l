for i = 0 to 5
  if i = 0
    print "zero"
  elif i = 1
    print "one"
  elif i = 2
    print "two"
  else
    print "many"
  endif
  if i > 3
    print "big"
  elif i = 3
    print "three"
  endif
next i
' Comments must be kept
a = 1
if a = 0
  print "zero"
else ' not zero
  if a = 1
    print "one"
  endif
endif
if a = 0
  print "zero"
elif a = 1 ' one?
  print "one"
endif ' end inner
if a = 0
  print "zero"
else
  if a = 1
    print "one"
  endif ' end inner
endif ' end outer
