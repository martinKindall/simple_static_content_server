## To consider when programming in C

- If many results need to be returned, use structures if possible, instead of using pointers and mutating.
- Do not use exit(), only the main() method can return. If nested functions need to abort, then return a status code within a structure that can be checked in main().
- Structure of source .c files, from top to bottom:
  - includes
  - method definitions without body (if any)
  - data structures
  - main method with body
  - method implementations
