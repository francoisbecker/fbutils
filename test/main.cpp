#include "lest.hpp"

#define CASE( name ) lest_CASE( specification, name )

lest::tests& specification()
{
    static lest::tests tests;
    return tests;
}

#if 0
CASE( "A passing test" "[pass]" )
{
    EXPECT( 42 == 42 );
}

CASE( "A failing test" "[fail]" )
{
    EXPECT( 42 == 7 );
}
#endif

int main(int argc, char* argv[])
{
    return lest::run( specification(), argc, argv /*, std::cout */ );
}

