#ifndef SLXP_H
#define SLXP_H

#include <string>
#include <vector>
#include <ostream>
#include <type_traits>
#include <sstream>

struct BinarySerializable {
    virtual std::ostream& serialize(std::ostream& os) const = 0;
};

struct JSONSerializable {
    virtual std::string toJSON() const = 0;
};

template <typename T>
std::ostream& unsafe_write(std::ostream& os, const T value) {
    return os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
std::ostream& unsafe_write(std::ostream& os, const T* array, const size_t len) {
    unsafe_write(os, len);
    for (size_t i = 0; i < len; i++)
        os.write(reinterpret_cast<const char*>(&array[i]), sizeof(T));
    return os;
}

template <typename T>
std::ostream& unsafe_write(std::ostream& os, const T* array[], const size_t len0, const size_t len1) {
    unsafe_write(os, len0);
    unsafe_write(os, len1);
    for (size_t i = 0; i < len0; i++)
        for (size_t j = 0; j < len1; j++)
            os.write(reinterpret_cast<const char*>(&array[i][j]), sizeof(T));
    return os;
}

std::ostream& unsafe_write(std::ostream& os, std::string value) {
    return unsafe_write(os, value.c_str(), value.size());
}

template <typename T>
std::ostream& unsafe_write(std::ostream& os, const std::vector<T>& values) {
    unsafe_write(os, values.size());
    for (auto& value : values)
        os.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return os;
}

template <typename T>
std::ostream& serialize_vector(std::ostream& os, const std::vector<T>& values) {
    //static_assert(std::is_base_of<Serializable, T>::value)
    unsafe_write(os, values.size());
    for (auto& value : values)
    {
        auto serializable = (const BinarySerializable*)&value;
        if(serializable)
            serializable->serialize(os);
    }
    return os;
}

typedef std::vector<unsigned short> Indices_list_t;
typedef float matrix_4x4_t[4][4];

std::string vectorToJSON(const Indices_list_t values) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < values.size(); i++)
    {
        os << values[i];
        if (i != (values.size() - 1))
            os << ",";
    }
    os << "]";
    return os.str();
}

inline std::ostream& arrayToJSON(std::ostream& os, const float* values, const size_t len)
{
    os << "[";
    for (size_t i = 0; i < len; i++)
    {
        os << values[i];
        if (i != (len - 1))
            os << ",";
    }
    os << "]";
    return os;
}

std::string arrayToJSON(const float* values, const size_t len)
{
    std::ostringstream os;
    arrayToJSON(os, values, len);
    return os.str();
}

std::string arrayToJSON(const float* values, const size_t len0, const size_t len1)
{
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < len0; i++)
    {
        arrayToJSON(os, &values[len1 * i], len1);
        if (i != (len0 - 1))
            os << ",";
    }
    os << "]";
    return os.str();
}

template <typename T>
std::string vectorToJSON(std::vector<T> values) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < values.size(); i++)
    {
        auto& value = values[i];
        auto serializable = (const JSONSerializable *)(&value);
        if (serializable)
            os << serializable->toJSON();
        if (i != (values.size() - 1))
            os << ",";
    }
    os << "]";
    return os.str();
}

// This is probably really slow and I don't caaaare
inline float sanitize(float x)
{
    if (!std::isfinite(x))
        if (std::isnan(x))
            return 0.f;
        else if (x > 0)
            return 10e30;
        else
            return -10e30;
    return x;
}

struct Vec2 : public BinarySerializable, public JSONSerializable {
    float x;
    float y;
    inline virtual std::ostream& serialize(std::ostream& os) const override
    {
        unsafe_write(os, x);
        unsafe_write(os, y);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "["
            << sanitize(x) << ","
            << sanitize(y) << "]";
        return os.str();
    }
};

struct Vec3 : public Vec2 {
    float z;
    inline virtual std::ostream& serialize(std::ostream& os) const override
    {
        Vec2::serialize(os);
        unsafe_write(os, z);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "["
            << sanitize(x) << ","
            << sanitize(y) << ","
            << sanitize(z) << "]";
        return os.str();
    }
};

struct Vec4 : public Vec3 {
    float w;
    inline virtual std::ostream& serialize(std::ostream& os) const override
    {
        Vec3::serialize(os);
        unsafe_write(os, w);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "["
            << sanitize(x) << ","
            << sanitize(y) << ","
            << sanitize(z) << ","
            << sanitize(w) << "]";
        return os.str();
    }
};

typedef std::vector<Vec2> Vec2_list_t;
typedef std::vector<Vec3> Vec3_list_t;
typedef std::vector<Vec4> Vec4_list_t;

struct WithTRS : public BinarySerializable, public JSONSerializable {
    Vec3 LocalPosition;
    Vec4 LocalRotation;
    Vec3 LocalScale;
    virtual std::ostream& serialize(std::ostream& os) const override
    {
        LocalPosition.serialize(os);
        LocalRotation.serialize(os);
        LocalScale.serialize(os);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "\"LocalPosition\": " << LocalPosition.toJSON() << "," << std::endl
            << "\"LocalRotation\": " << LocalRotation.toJSON() << "," << std::endl
            << "\"LocalScale\": " << LocalScale.toJSON();
        return os.str();
    }
};

struct WithName {
    std::string Name;
    WithName(std::string name) :
        Name(name)
    { }
};

struct WithId {
    unsigned int Id;
    WithId(unsigned int id):
        Id(id)
    { }
};

struct WithParentId {
    unsigned int ParentId;
    WithParentId(unsigned int parent_id) :
        ParentId(parent_id)
    { }
};

// -------- //

struct SLXPFace : public BinarySerializable, public JSONSerializable {
    Vec3_list_t Positions;
    Vec3_list_t Normals;
    Vec3_list_t Tangents;
    Vec2_list_t TexCoords;
    Vec4_list_t Weights;
    Indices_list_t Indices;

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        serialize_vector(os, Positions);
        serialize_vector(os, Normals);
        serialize_vector(os, Tangents);
        serialize_vector(os, TexCoords);
        serialize_vector(os, Weights);
        serialize_vector(os, Indices);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << "\"Positions\": " << vectorToJSON(Positions) << "," << std::endl
            << "\"Normals\": " << vectorToJSON(Normals) << "," << std::endl
            << "\"Tangents\": " << vectorToJSON(Tangents) << "," << std::endl
            << "\"TexCoords\": " << vectorToJSON(TexCoords) << "," << std::endl
            << "\"Weights\": " << vectorToJSON(Weights) << "," << std::endl
            << "\"Indices\": " << vectorToJSON(Indices) << std::endl
            << "}";
        return os.str();
    }
};

struct SLXPObjectBaseMixin:
    public WithName,
    public WithId,
    public WithParentId,
    public WithTRS
{
    SLXPObjectBaseMixin(std::string name, unsigned int id, unsigned int parent_id = 0):
        WithName(name),
        WithId(id),
        WithParentId(parent_id)
    { }

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        unsafe_write(os, Name);
        WithTRS::serialize(os);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "\"Name\": \"" << Name << "\"," << std::endl
            << "\"Id\": " << Id << "," << std::endl
            << "\"ParentId\": " << ParentId << "," << std::endl
            << WithTRS::toJSON();
        return os.str();
    }
};

struct SLXPObject : public SLXPObjectBaseMixin
{
private:
    bool _HasBindShapeMatrix;
public:
    std::vector<SLXPFace> Faces;
    
    float BindShapeMatrix[4][4] = { };

    SLXPObject(std::string name, unsigned int id, unsigned int parent_id = 0):
        SLXPObjectBaseMixin(name, id, parent_id),
        _HasBindShapeMatrix(false)
    { }

    void setBindShapeMatrix(const matrix_4x4_t mtx) {
        auto src = &mtx[0][0];
        std::copy(src, src + 16, &BindShapeMatrix[0][0]);
        _HasBindShapeMatrix = true;
    }

    void unsetBindShapeMatrix()
    {
        //auto dst = &BindShapeMatrix[0][0];
        //std::fill(dst, dst + 16, 0.f);
        _HasBindShapeMatrix = false;
    }

    bool hasBindShapeMatrix() const
    {
        return _HasBindShapeMatrix;
    }

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        SLXPObjectBaseMixin::serialize(os);
        unsafe_write(os, Faces);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << SLXPObjectBaseMixin::toJSON() << "," << std::endl;

        if (_HasBindShapeMatrix)
            os << "\"BindShapeMatrix\": " << arrayToJSON((const float*)BindShapeMatrix, 4, 4) << "," << std::endl;

        os << "\"Faces\": " << vectorToJSON(Faces) << std::endl
            << "}";
        return os.str();
    }
};

struct SLXPJoint : public SLXPObjectBaseMixin
{

    SLXPJoint(std::string name, unsigned int id, unsigned int parent_id = 0) :
        SLXPObjectBaseMixin(name, id, parent_id)
    {
    }

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        SLXPObjectBaseMixin::serialize(os);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << SLXPObjectBaseMixin::toJSON() << std::endl
            << "}";
        return os.str();
    }
};

struct SLXPCollection : public BinarySerializable, public JSONSerializable {
    std::vector<SLXPObject> Objects;
    std::vector<SLXPJoint> Joints;

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        serialize_vector(os, Objects);
        return os;
    }
    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << "\"Objects\": " << vectorToJSON(Objects) << "," << std::endl
            << "\"Joints\": " << vectorToJSON(Joints) << std::endl
            << "}";
        return os.str();
    }
};

struct SLXP : public BinarySerializable, public JSONSerializable {
    const unsigned char FormatVersionA = 0;
    const unsigned char FormatVersionB = 0;
    const unsigned char FormatVersionC = 1;
    std::string Title;
    SLXPCollection Collection;

    SLXP(std::string title) :
        Title(title)
    {
    }

    virtual std::ostream& serialize(std::ostream& os) const override
    {
        unsafe_write(os, 'S');
        unsafe_write(os, 'L');
        unsafe_write(os, 'X');
        unsafe_write(os, 'P');
        unsafe_write(os, FormatVersionA);
        unsafe_write(os, FormatVersionB);
        unsafe_write(os, FormatVersionC);
        unsafe_write(os, Title);
        Collection.serialize(os);
        return os;
    }

    virtual std::string toJSON() const override
    {
        std::ostringstream os;
        os << "{" << std::endl
            << "\"Title\": \"" << Title << "\", " << std::endl
            << "\"Collection\": " << Collection.toJSON() << std::endl
            << "}";
        return os.str();
    }
};

#endif