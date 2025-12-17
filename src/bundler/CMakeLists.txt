


add_executable(generator generator.cpp)




function(generate OUTPUT INPUT)
  add_custom_command(
    OUTPUT ${OUTPUT}
    COMMAND generator ${INPUT} ${OUTPUT}
    DEPENDS generator ${INPUT}
    COMMENT "Generating ${OUTPUT} from ${INPUT}"
    VERBATIM
  )
endfunction(generate)

generate(
  ${CMAKE_CURRENT_SOURCE_DIR}/foo.txt.inc
  ${CMAKE_CURRENT_SOURCE_DIR}/foo.txt)



add_executable(bundler main.cpp 
  ${CMAKE_CURRENT_SOURCE_DIR}/foo.txt.inc
)