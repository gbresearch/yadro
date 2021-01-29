//-----------------------------------------------------------------------------
//  GB Research, LLC (c) 2006. All Rights Reserved
//  No part of this file may be reproduced, stored in a retrieval system,
//  or transmitted, in any form, or by any means, electronic, mechanical,
//  photocopying, recording, or otherwise, without the prior written
//  permission.
//-----------------------------------------------------------------------------

#include "../include/static_string.h"


namespace gbr {
    namespace container {
        //*****************************************
        namespace {
            static void foo()
            {
                static_string<10> str("hello");
                str += " world!";
                str += static_string<5>("aha");
                static_string<10, char> str1("hello");
                str1 += str.c_str();
                static_string<10> str2(std::string("hello"));
            }
        }
    }
} // namespaces
