# Amstrad Plus sprite compiler

syntaxe is: compiler file1 [file2] [options]

# options:
-idx <sequence(s)>  define sprite indexes to compute inside a binary file

-c     compile a full sprite

-d     compile difference between two sprites or a sequence

-meta  compile consecutive sprites

-noret do not add RET at the end of the routine

-inch  add a INC H at the end of the routine

-jpix  add a JP (IX) at the end of the routine

-jpiy  add a JP (IY) at the end of the routine

-l <label>  insert a numbered label for multi diff


# looping sequence compilation from a 64 sprites sheet:
  
; use same file for source and destination
  
; describe one sequence for source and one sequence for destination
  
; this will generate transitions from sprites 0,1,2,3,4..., 62 to sprites 1,2,3,4,5...,63
  
compiler asteroids16x16x64.bin asteroids16x16x64.bin -idx 0-62 -idx 1-63 -l astroide -d >  astro.asm
  
; this will generate the transition from sprite 63 to sprite 0
  
compiler asteroids16x16x64.bin asteroids16x16x64.bin -idx 63   -idx 0    -l astroide -d >> astro.asm
  
; you can mutualise both lines like this
  
; first sequence to 0 from 63, destination sequence from 1 to 63 + 0 at the end!
  
compiler asteroids16x16x64.bin asteroids16x16x64.bin -idx 0-63   -idx 1-63,0    -l astroide -d > astro.asm
