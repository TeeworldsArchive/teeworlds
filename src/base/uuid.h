#ifndef BASE_UUID_H
#define BASE_UUID_H

// modify from https://github.com/ddnet/ddnet
#ifdef __cplusplus
extern "C" {
#endif

enum
{
	UUID_MAXSTRSIZE = 37, // 12345678-0123-5678-0123-567890123456

	UUID_INVALID = -2,
	UUID_UNKNOWN = -1,

	OFFSET_UUID = 1 << 16,
};

typedef struct
{
	unsigned char m_aData[16];
} Uuid;

extern const Uuid UUID_ZEROED;

Uuid random_uuid();
Uuid calculate_uuid(const char *name);
// The buffer length should be at least UUID_MAXSTRSIZE.
void format_uuid(Uuid uuid, char *buffer, int size);
// Returns nonzero on failure.
int parse_uuid(Uuid *uuid, const char *buffer);

int uuid_comp(Uuid uuid1, Uuid uuid2);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
inline bool operator==(const Uuid &that, const Uuid &other)
{
	return uuid_comp(that, other) == 0;
}

inline bool operator!=(const Uuid &that, const Uuid &other)
{
	return !(that == other);
}
#endif

#endif // BASE_UUID_H
