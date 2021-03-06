/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <cbsasl/cbsasl_internal.h>
#include <cbsasl/pwconv.h>
#include <cbsasl/user.h>
#include <fstream>
#include <iomanip>
#include <iostream>

int main(void) {
    std::ofstream isaslpw("isasl.pw");

    isaslpw << "_admin password" << std::endl
            << "bucket-1 1S|=,%#x1" << std::endl
            << "bucket-2 secret" << std::endl << std::setfill('0');

    for (int ii = 0; ii < 100; ++ii) {
        isaslpw << "mybucket_" << std::setw(3) << ii
                << " mybucket_" << std::setw(3) << ii << std::endl;
    }

    isaslpw.close();

    Couchbase::UserFactory::setDefaultHmacIterationCount(10);
    cbsasl_pwconv("isasl.pw", "cbsaslpw.json");

    return EXIT_SUCCESS;
}
