' Strings with special characters
? "Quote "" inside"
? "Line"$9B"break"
? "Score: "; 10; " "; CHR$(65); CHR$(66)
A$ = "Hello"
A$ =+ " World"
? A$[1, 5], LEN(A$)
' CHR$ uses a shared buffer: this prints 1
? CHR$(65) = CHR$(66)
? "AB" = "AB", A$ = "Hello World"
B$ = "Hello"
? B$, "Hello", "Hello", "Hello"
