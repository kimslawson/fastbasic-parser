' Optimizations
DIM arr(10) WORD
X = 10
POKE 704 + 4, 15 * 16
GRAPHICS 8 + 16
GRAPHICS 0
FOR I = 0 TO 10 STEP 1
  IF X <> 0
    ? I;
  ENDIF
  IF I > 5
    X = X - 1
  ENDIF
  arr(I) = arr(I) + 1
NEXT I
X = X + 1
PAUSE 0
? PEEK(712) + ABS(-X) + RAND(10) * 0
? (X * 2) + (X / 2)
? "A"; "B"; CHR$(67)
END
