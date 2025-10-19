#pragma once

#include <cstdlib>

#if _MSC_VER
#include <malloc.h>
#endif

inline unsigned char c_bittestandset(long* p_Val, long p_Bit)
{
	unsigned char s_BitSet = (*p_Val >> p_Bit) & 1;
	*p_Val = *p_Val | (1 << p_Bit);
	return s_BitSet;
}

inline void* c_aligned_alloc(size_t p_Size, size_t p_Alignment)
{
#if _MSC_VER
	return _aligned_malloc(p_Size, p_Alignment);
#elif __EMSCRIPTEN__
	return malloc((p_Size + (p_Alignment - 1)) & (-p_Alignment));
#else
	return std::aligned_alloc(p_Alignment, p_Size);
#endif
}

inline void c_aligned_free(void* p_Memory)
{
#if _MSC_VER
	return _aligned_free(p_Memory);
#elif __EMSCRIPTEN__
	free(p_Memory);
#else
	return std::free(p_Memory);
#endif
}

// Already defined in NavPower
//inline uint32_t c_byteswap_ulong(uint32_t p_Value)
//{
//#if _MSC_VER
//	return _byteswap_ulong(p_Value);
//#else
//	return ((p_Value >> 24) & 0x000000FF) | 
//			((p_Value >> 8) & 0x0000FF00) | 
//			((p_Value << 8) & 0x00FF0000) | 
//			((p_Value << 24) & 0xFF000000);
//#endif
//}

inline constexpr size_t c_get_aligned(size_t p_Size, size_t p_Alignment)
{
	if ((p_Size % p_Alignment) == 0)
		return p_Size;
	
	return p_Size + (p_Alignment - (p_Size % p_Alignment));
}