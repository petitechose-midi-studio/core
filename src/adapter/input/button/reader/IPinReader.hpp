#pragma once

class IPinReader {
public:
    virtual ~IPinReader() = default;

    virtual bool read() = 0;

    virtual void initialize() {}

    virtual void update() {}
};
