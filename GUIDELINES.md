## To consider when programming in C

1. If many results need to be returned, use structures if possible, instead of using pointers and mutating.
2. Do not use exit(), only the main() method can return. If nested functions need to abort, then return a status code within a structure that can be checked in main().
3. Structure of source .c files, from top to bottom:
  - includes
  - data structures
  - method definitions without body (if any)
  - main method with body
  - method implementations

## Claude code

1. Read .gitignore and also ignore these files when exploring the project with "tree" or similar commands.

