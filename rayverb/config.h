#include "rayverb.h"
#include "helpers.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/error/en.h"
#include "rapidjson/document.h"

#include <string>
#include <stdexcept>
#include <map>

struct HrtfConfig
{
    cl_float3 facing;
    cl_float3 up;
};

struct JsonValidatorBase
{
    virtual void run (const rapidjson::Value & value) const = 0;
};

struct OptionalValidator;
struct RequiredValidator;

template <typename T, typename U> struct JsonValidator;

class ConfigValidator
{
public:
    template <typename T>
    void addOptionalValidator (const std::string & s, T & t)
    {
        validators.emplace_back (new JsonValidator <T, OptionalValidator> (s, t));
    }

    template <typename T>
    void addRequiredValidator (const std::string & s, T & t)
    {
        validators.emplace_back (new JsonValidator <T, RequiredValidator> (s, t));
    }

    virtual void run (const rapidjson::Value & value) const
    {
        for (const auto & i : validators)
            i->run (value);
    }

private:
    std::vector <std::unique_ptr <JsonValidatorBase>> validators;
};


struct StringWrapper
{
    StringWrapper (const std::string & s): s (s) {}
    const std::string & s;
};

struct Validator: public StringWrapper
{
    Validator (const std::string & s): StringWrapper (s) {}
    virtual bool validate (const rapidjson::Value & value) const = 0;
};

struct RequiredValidator: public Validator
{
    RequiredValidator (const std::string & s): Validator (s) {}
    bool validate (const rapidjson::Value & value) const
    {
        if (! value.HasMember (s.c_str()))
            throw std::runtime_error ("key " + s + " not found in config object");
        return true;
    }
};

struct OptionalValidator: public Validator
{
    OptionalValidator (const std::string & s): Validator (s) {}
    bool validate (const rapidjson::Value & value) const
    {
        return value.HasMember (s.c_str());
    }
};

template <typename T>
struct JsonGetter
{
    JsonGetter (T & t): t (t) {}
    virtual bool check (const rapidjson::Value & value) const = 0;
    virtual void get (const rapidjson::Value & value) const = 0;
    T & t;
};

template<>
struct JsonGetter<double>
{
    JsonGetter (double & t): t (t) {}
    virtual bool check (const rapidjson::Value & value) const
    {
        return value.IsNumber();
    }
    virtual void get (const rapidjson::Value & value) const
    {
        t = value.GetDouble();
    }
    double & t;
};

template<>
struct JsonGetter<float>
{
    JsonGetter (float & t): t (t) {}
    virtual bool check (const rapidjson::Value & value) const
    {
        return value.IsNumber();
    }
    virtual void get (const rapidjson::Value & value) const
    {
        t = value.GetDouble();
    }
    float & t;
};

template<>
struct JsonGetter<bool>
{
    JsonGetter (bool & t): t (t) {}
    virtual bool check (const rapidjson::Value & value) const
    {
        return value.IsBool();
    }
    virtual void get (const rapidjson::Value & value) const
    {
        t = value.GetBool();
    }
    bool & t;
};

template<>
struct JsonGetter<int>
{
    JsonGetter (int & t): t (t) {}
    virtual bool check (const rapidjson::Value & value) const
    {
        return value.IsInt();
    }
    virtual void get (const rapidjson::Value & value) const
    {
        t = value.GetInt();
    }
    int & t;
};

template<>
struct JsonGetter<cl_float3>
{
    JsonGetter (cl_float3 & t): t (t) {}
    virtual bool check (const rapidjson::Value & value) const
    {
        if (! value.IsArray())
            return false;
        auto components = 3;
        if (value.Size() != components)
            return false;
        for (auto i = 0; i != components; ++i)
        {
            if (! value [i].IsNumber())
                return false;
        }
        return true;
    }
    virtual void get (const rapidjson::Value & value) const
    {
        t = (cl_float3)
        {   {   static_cast <cl_float> (value [0].GetDouble())
            ,   static_cast <cl_float> (value [1].GetDouble())
            ,   static_cast <cl_float> (value [2].GetDouble())
            }
        };
    }
    cl_float3 & t;
};

template<>
struct JsonGetter<cl_float8>
{
    JsonGetter (cl_float8 & t): t (t) {}
    virtual bool check (const rapidjson::Value & value) const
    {
        if (! value.IsArray())
            return false;
        auto components = sizeof (cl_float8) / sizeof (float);
        if (value.Size() != components)
            return false;
        for (auto i = 0; i != components; ++i)
        {
            if (! value [i].IsNumber())
                return false;
        }
        return true;
    }
    virtual void get (const rapidjson::Value & value) const
    {
        t = (cl_float8)
        {   {   static_cast <cl_float> (value [0].GetDouble())
            ,   static_cast <cl_float> (value [1].GetDouble())
            ,   static_cast <cl_float> (value [2].GetDouble())
            ,   static_cast <cl_float> (value [3].GetDouble())
            ,   static_cast <cl_float> (value [4].GetDouble())
            ,   static_cast <cl_float> (value [5].GetDouble())
            ,   static_cast <cl_float> (value [6].GetDouble())
            ,   static_cast <cl_float> (value [7].GetDouble())
            }
        };
    }
    cl_float8 & t;
};

template<>
struct JsonGetter<Surface>
{
    JsonGetter (Surface & t): t (t) {}
    virtual bool check (const rapidjson::Value & value) const
    {
        return value.IsObject();
    }
    virtual void get (const rapidjson::Value & value) const
    {
        ConfigValidator cv;

        cv.addRequiredValidator ("specular", t.specular);
        cv.addRequiredValidator ("diffuse", t.diffuse);

        cv.run (value);
    }
    Surface & t;
};

template<>
struct JsonGetter<RayverbFiltering::FilterType>
{
    JsonGetter (RayverbFiltering::FilterType & t)
    :   t (t)
    ,   stringkeys
        (   {   {"sinc", RayverbFiltering::FILTER_TYPE_WINDOWED_SINC}
            ,   {"onepass", RayverbFiltering::FILTER_TYPE_BIQUAD_ONEPASS}
            ,   {"twopass", RayverbFiltering::FILTER_TYPE_BIQUAD_TWOPASS}
            }
        )
    {}
    virtual bool check (const rapidjson::Value & value) const
    {
        return value.IsString() &&
        (   any_of
            (   stringkeys.begin()
            ,   stringkeys.end()
            ,   [&value] (const std::pair <std::string, RayverbFiltering::FilterType> & i)
                {
                    return i.first == value.GetString();
                }
            )
        );
    }
    virtual void get (const rapidjson::Value & value) const
    {
        t = stringkeys.at (value.GetString());
    }
    RayverbFiltering::FilterType & t;
    const std::map <std::string, RayverbFiltering::FilterType> stringkeys;
};

template<>
struct JsonGetter<Speaker>
{
    JsonGetter (Speaker & t): t (t){}
    virtual bool check (const rapidjson::Value & value) const
    {
        return value.IsObject();
    }
    virtual void get (const rapidjson::Value & value) const
    {
        ConfigValidator cv;

        cv.addRequiredValidator ("direction", t.direction);
        cv.addRequiredValidator ("shape", t.coefficient);

        cv.run (value);
    }
    Speaker & t;
};

template<>
struct JsonGetter<HrtfConfig>
{
    JsonGetter (HrtfConfig & t): t (t){}
    virtual bool check (const rapidjson::Value & value) const
    {
        return value.IsObject();
    }
    virtual void get (const rapidjson::Value & value) const
    {
        ConfigValidator cv;

        cv.addRequiredValidator ("facing", t.facing);
        cv.addRequiredValidator ("up", t.up);

        cv.run (value);

        normalize (t.facing);
        normalize (t.up);
    }
    HrtfConfig & t;
private:
    static void normalize (cl_float3 & v)
    {
        cl_float len =
            1.0 / sqrt (v.s [0] * v.s [0] + v.s [1] * v.s [1] + v.s [2] * v.s [2]);
        for (auto i = 0; i != sizeof (cl_float3) / sizeof (float); ++i)
        {
            v.s [i] *= len;
        }
    }
};

template <typename T>
struct JsonGetter<std::vector <T>>
{
    JsonGetter (std::vector <T> & t): t (t){}
    virtual bool check (const rapidjson::Value & value) const
    {
        return value.IsArray();
    }
    virtual void get (const rapidjson::Value & value) const
    {
        for (auto i = value.Begin(); i != value.End(); ++i)
        {
            T temp;
            JsonGetter <T> getter (temp);
            if (! getter.check (*i))
            {
                throw std::runtime_error ("invalid value in array");
            }
            getter.get (*i);
            t.push_back (temp);
        }
    }
    std::vector <T> & t;
};

template <typename T, typename U>
struct JsonValidator: public JsonValidatorBase, public JsonGetter <T>, public U
{
    JsonValidator (const std::string & s, T & t): JsonGetter <T> (t), U (s) {}

    virtual void run (const rapidjson::Value & value) const
    {
        if (U::validate (value))
        {
            if (! JsonGetter<T>::check (value [U::s.c_str()]))
            {
                throw std::runtime_error ("invalid value for key " + U::s);
            }
            JsonGetter<T>::get (value [U::s.c_str()]);
        }
    }
};

