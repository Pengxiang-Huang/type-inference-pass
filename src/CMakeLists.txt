# add opt load libarary 
add_llvm_library(SVFTypeInferPass MODULE
	SVFTypeInferPass.cpp
)

# install 
install(TARGETS SVFTypeInferPass 
	LIBRARY DESTINATION lib
)

target_link_libraries(SVFTypeInferPass PRIVATE SvfLLVM SvfCore)
