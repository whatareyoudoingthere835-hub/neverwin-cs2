#include "base_handle.h"

#include <core/interfaces/interfaces.h>

c_base_handle::c_base_handle( ) noexcept : index( INVALID_EHANDLE_INDEX ) {}

c_base_handle::c_base_handle( const int entry, const int serial ) noexcept
{
	index = entry | (serial << NUM_SERIAL_NUM_SHIFT_BITS);
}

bool c_base_handle::operator!=( const c_base_handle& other ) const noexcept
{
	return index != other.index;
}

bool c_base_handle::operator==( const c_base_handle& other ) const noexcept
{
	return index == other.index;
}

bool c_base_handle::operator<( const c_base_handle& other ) const noexcept
{
	return index < other.index;
}

bool c_base_handle::is_valid( ) const noexcept
{
	return index != INVALID_EHANDLE_INDEX;
}

int c_base_handle::get_entry_index( ) const noexcept
{
	return static_cast<int>(index & ENT_ENTRY_MASK);
}

int c_base_handle::get_serial_number( ) const noexcept
{
	return static_cast<int>(index >> NUM_SERIAL_NUM_SHIFT_BITS);
}

int c_base_handle::to_int( ) const
{
	return static_cast<int>(index);
}

void* c_base_handle::get_base_entity( )
{
	return g_interfaces->m_entity_system->get_base_entity( index & 0x7FFF ).as<void*>( );
}