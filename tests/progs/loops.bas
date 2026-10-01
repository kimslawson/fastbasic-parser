' Control structures
I = 0
DO
  INC I
  IF I > 3 THEN EXIT
LOOP
WHILE I > 0
  DEC I
  REPEAT
    I = I - 1
  UNTIL I < 2
WEND
IF I = 0
  ? "zero"
ELIF I = 1
  ? "one"
ELSE
  ? "other"
ENDIF
. Old style comment
FOR J = 1 TO 3 : ? J; : NEXT J ' trailing comment
?
