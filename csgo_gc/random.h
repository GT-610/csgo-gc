#pragma once

class Random
{
public:
    Random()
        : m_engine{ std::random_device{}() }
    {
    }

    explicit Random(uint32_t seed)
        : m_engine{ seed }
    {
    }

    template<typename T>
    T Integer(T min, T max)
    {
        return std::uniform_int_distribution<T>{ min, max }(m_engine);
    }

    template<typename T>
    T Integer()
    {
        return std::uniform_int_distribution<T>{}(m_engine);
    }

    float Float(float min = 0.0f, float max = 1.0f)
    {
        return std::uniform_real_distribution<float>{ min, max }(m_engine);
    }

private:
    std::mt19937 m_engine;
};
