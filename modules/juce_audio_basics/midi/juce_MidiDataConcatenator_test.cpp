/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

class BytestreamSysexExtractorTest : public UnitTest
{
public:
    BytestreamSysexExtractorTest()
        : UnitTest ("BytestreamSysexExtractor", UnitTestCategories::midi)
    {
    }

    void runTest() override
    {
        beginTest ("Passing empty buffer while no message is in progress does nothing");
        {
            BytestreamSysexExtractor extractor;
            bool called = false;
            extractor.push ({}, [&] (auto&&...) { called = true; });

            expect (! called);
        }

        beginTest ("Passing sysex with no payload reports an empty message");
        {
            BytestreamSysexExtractor extractor;
            bool called = false;
            const uint8_t message[] { uint8_t (0xf0), uint8_t (0xf7) };
            extractor.push (message, [&] (auto status, auto bytes)
            {
                called = true;
                expect (status == SysexExtractorCallbackKind::lastSysex);
                expect (bytes.size() == 2 && bytes[0] == uint8_t (0xf0) && bytes[1] == uint8_t (0xf7));
            });

            expect (called);
        }

        beginTest ("Sending only the sysex starting byte reports an ongoing message");
        {
            BytestreamSysexExtractor extractor;
            int numCalls = 0;
            const uint8_t message[] { uint8_t (0xf0) };
            extractor.push (message, [&] (auto status, auto bytes)
            {
                ++numCalls;
                expect (status == SysexExtractorCallbackKind::ongoingSysex);
                expect (bytes.size() == 1 && bytes[0] == uint8_t (0xf0));
            });

            expect (numCalls == 1);

            // Sending a subsequent empty span should report an ongoing message
            extractor.push (Span<const uint8_t>{}, [&] (auto status, auto bytes)
            {
                ++numCalls;
                expect (status == SysexExtractorCallbackKind::ongoingSysex);
                expect (bytes.empty());
            });

            expect (numCalls == 2);
        }

        beginTest ("Sending sysex interspersed with realtime messages filters out the realtime messages");
        {
            BytestreamSysexExtractor extractor;
            const uint8_t message[] { uint8_t (0xf0),
                                        uint8_t (0x50), // first data byte
                                        uint8_t (0xfe), // active sensing
                                        uint8_t (0x60), // second data byte
                                        uint8_t (0x70), // third data byte
                                        uint8_t (0xf7) };
            std::vector<std::vector<uint8_t>> vectors;
            extractor.push (message, [&] (auto, auto bytes) { vectors.emplace_back (bytes.begin(), bytes.end()); });

            expect (vectors == std::vector { std::vector { uint8_t (0xf0), uint8_t (0x50) },
                                             std::vector { uint8_t (0xfe) },
                                             std::vector { uint8_t (0x60), uint8_t (0x70), uint8_t (0xf7) } });
        }

        beginTest ("Sending a second f0 byte during an ongoing sysex terminates the previous sysex");
        {
            BytestreamSysexExtractor extractor;
            const uint8_t message[] { uint8_t (0xf0), // start of first sysex
                                        uint8_t (0x00),
                                        uint8_t (0x01),
                                        uint8_t (0xf0), // start of second sysex
                                        uint8_t (0x02),
                                        uint8_t (0x03) };

            std::vector<std::vector<uint8_t>> vectors;
            extractor.push (message, [&] (auto, auto bytes) { vectors.emplace_back (bytes.begin(), bytes.end()); });

            expect (vectors == std::vector { std::vector { uint8_t (0xf0), uint8_t (0x00), uint8_t (0x01) },
                                             std::vector { uint8_t (0xf0), uint8_t (0x02), uint8_t (0x03) } });
        }

        beginTest ("Status bytes truncate ongoing sysex");
        {
            BytestreamSysexExtractor extractor;
            const uint8_t message[] { uint8_t (0xf0), // start of first sysex
                                        uint8_t (0x10),
                                        uint8_t (0x20),
                                        uint8_t (0x30),
                                        uint8_t (0x80), // status byte
                                        uint8_t (0x00),
                                        uint8_t (0x00) };

            std::vector<std::vector<uint8_t>> vectors;
            extractor.push (message, [&] (auto, auto bytes) { vectors.emplace_back (bytes.begin(), bytes.end()); });

            expect (vectors == std::vector { std::vector { uint8_t (0xf0), uint8_t (0x10), uint8_t (0x20), uint8_t (0x30) },
                                             std::vector { uint8_t (0x80), uint8_t (0x00), uint8_t (0x00) } });
        }

        beginTest ("Running status is preserved between calls");
        {
            BytestreamSysexExtractor extractor;
            const uint8_t message[] { uint8_t (0x90), // note on
                                        uint8_t (0x10),
                                        uint8_t (0x20),
                                        uint8_t (0x30),
                                        uint8_t (0x40),
                                        uint8_t (0x50) };

            std::vector<std::vector<uint8_t>> vectors;
            const auto callback = [&] (auto status, auto bytes)
            {
                expect (status == SysexExtractorCallbackKind::notSysex);
                vectors.emplace_back (bytes.begin(), bytes.end());
            };
            extractor.push (message, callback);
            extractor.push (std::array { uint8_t (0x60) }, callback);

            expect (vectors == std::vector { std::vector { uint8_t (0x90), uint8_t (0x10), uint8_t (0x20) },
                                             std::vector { uint8_t (0x90), uint8_t (0x30), uint8_t (0x40) },
                                             std::vector { uint8_t (0x90), uint8_t (0x50), uint8_t (0x60) } });
        }

        beginTest ("Realtime messages can intersperse bytes of non-sysex messages");
        {
            BytestreamSysexExtractor extractor;
            const uint8_t message[] { uint8_t (0xd0),   // channel pressure
                                        uint8_t (0xfe),   // active sensing
                                        uint8_t (0x70),   // pressure cont.
                                        uint8_t (0xfe),   // active sensing
                                        uint8_t (0x60),   // second pressure message
                                        uint8_t (0xfe),   // active sensing
                                        uint8_t (0x50) }; // third pressure message

            std::vector<std::vector<uint8_t>> vectors;
            extractor.push (message, [&] (auto, auto bytes)
            {
                vectors.emplace_back (bytes.begin(), bytes.end());
            });

            expect (vectors == std::vector { std::vector { uint8_t (0xfe) },
                                             std::vector { uint8_t (0xd0), uint8_t (0x70) },
                                             std::vector { uint8_t (0xfe) },
                                             std::vector { uint8_t (0xd0), uint8_t (0x60) },
                                             std::vector { uint8_t (0xfe) },
                                             std::vector { uint8_t (0xd0), uint8_t (0x50) } });
        }

        beginTest ("Non-status bytes with no associated running status are ignored");
        {
            BytestreamSysexExtractor extractor;
            const uint8_t message[] { uint8_t (0x10),
                                        uint8_t (0x2e),
                                        uint8_t (0x30),
                                        uint8_t (0x4e),
                                        uint8_t (0x80),   // note off
                                        uint8_t (0x0e),
                                        uint8_t (0x00),
                                        uint8_t (0xf0),   // sysex
                                        uint8_t (0xf7),   // end sysex
                                        uint8_t (0x00),   // sysex resets running status
                                        uint8_t (0x10), };

            std::vector<std::vector<uint8_t>> vectors;
            extractor.push (message, [&] (auto, auto bytes)
            {
                vectors.emplace_back (bytes.begin(), bytes.end());
            });

            expect (vectors == std::vector { std::vector { uint8_t (0x80), uint8_t (0x0e), uint8_t (0x00) },
                                             std::vector { uint8_t (0xf0), uint8_t (0xf7) } });
        }
    }
};

static BytestreamSysexExtractorTest bytestreamSysexExtractorTest;

} // namespace juce
