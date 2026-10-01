' Minified-style program with constant variables
B = 53248
C = 704
H$ = "Score: "
D = -1
poke C + 1, 15
for I = 0 to 3
  poke B + I, I * 16
next I
print H$; D; " "; H$
X = 5
X = X + 1
print X, -B
if B = 1
  print 1
elif C <> 0
  print 2
endif
' Not fixed: used before by a PROC
exec SHOW
E = 10

proc SHOW
  print E
endproc
