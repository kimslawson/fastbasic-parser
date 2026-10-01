' Minified-style program with constant variables
B=53248:C=704:H$="Score: ":D=-1
POKE C+1, 15
FOR I=0 TO 3 : POKE B+I, I*16 : NEXT I
? H$; D; " "; H$
X=5 : X=X+1
? X, -B
IF B=1
  ? 1
ELSE
  IF C<>0
    ? 2
  ENDIF
ENDIF
' Not fixed: used before by a PROC
EXEC SHOW
E=10
PROC SHOW
  ? E
ENDPROC
